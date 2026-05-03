/*
 * Copyright (c) 2026 Alessandro Martone
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <string.h>

#include "header_parser.h"

#ifndef bool
   #ifdef _MSC_VER
      #define bool  signed char
      #define true  1
      #define false 0
   #else
      #include <stdbool.h>
   #endif
#endif

#ifndef INLINE
   #ifdef _MSC_VER
      #define INLINE __inline
   #elif defined(__STDC__) && __STDC_VERSION__ >= 199901L
      #define INLINE inline
   #else
      #define INLINE
   #endif
#endif

typedef enum TokenKind {
   TK_NULL = 0,
   TK_EOF = 1,

   TK_WSP,       // ' ' | '\t'
   TK_DQUOTE,    // '"'
   TK_ALPHANUM,  // '0' - '9' | 'A' - 'Z' | 'a' - 'z'
   TK_OCHAR,     // '!' - '~'
   TK_OBS_TEXT,  // 0x80 - 0xFF
} TokenKind;

typedef struct Token {
   char c;
   TokenKind kind;
} Token;

typedef struct Lexer {
   const char *pos;
   const char *end;
} Lexer;

static void lx_init(Lexer *lx, const char *data, size_t len)
{
   lx->pos = data;
   lx->end = data + len;
}

static void lx_next(Lexer *lx, Token *tok)
{
   TokenKind kind;
   char c;

   if (lx->pos == lx->end) {
      c = 0;
      kind = TK_EOF;
   }
   else {
      c = *(lx->pos++);
      kind = TK_NULL;

      switch (c) {
      case ' ':
      case '\t':
         kind = TK_WSP;
         break;
      case '"':
         kind = TK_DQUOTE;
         break;
      default:
         if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            kind = TK_ALPHANUM;
         else if (c >= '!' && c <= '~')  // visible chars that aren't ALPHANUM
            kind = TK_OCHAR;
         else if ((unsigned char)c >= 0x80 && (unsigned char)c <= 0xFF)
            kind = TK_OBS_TEXT;
         break;
      }
   }

   tok->c = c;
   tok->kind = kind;
}

INLINE static const char *tok_strchr(Token *tok, char *s)
{
   return tok->kind > TK_EOF ? strchr(s, tok->c) : NULL;
}

typedef struct HeaderParser {
   Lexer lx;
   Token tok;  // last token read
   HeaderCallbacks callbacks;
} HeaderParser;

/**
 * used for two things:
 * - revert changes if parsing fails (everywhere)
 * - tracking of "semantically useful" substring (when necessary)
 */
typedef struct Sema {
   HeaderParser *hp;
   const char *pos;
   size_t len;
} Sema;

INLINE static void sema_beg(Sema *sema, HeaderParser *hp)
{
   sema->hp = hp;
   sema->pos = hp->lx.pos;
   sema->len = 0;
}

INLINE static bool sema_end(Sema *sema, bool okay)
{
   HeaderParser *hp = sema->hp;

   if (okay)
      sema->len = (size_t)(hp->lx.pos - sema->pos);
   else
      hp->lx.pos = sema->pos;

   return okay;
}

void hp_init(HeaderParser *hp, const char *data, size_t len_data, HeaderCallbacks *callbacks)
{
   memset(hp, 0, sizeof(*hp));

   lx_init(&hp->lx, data, len_data);
   memcpy(&hp->callbacks, callbacks, sizeof(hp->callbacks));
}

INLINE static Token *hp_next(HeaderParser *hp)
{
   lx_next(&hp->lx, &hp->tok);

   return &hp->tok;
}

INLINE static void hp_callback_val(HeaderParser *hp, Sema *sema)
{
   if (hp->callbacks.on_val)
      hp->callbacks.on_val(hp->callbacks.usrdata, sema->pos, sema->len);
}

INLINE static void hp_callback_param(HeaderParser *hp, Sema *sema_k, Sema *sema_v)
{
   if (hp->callbacks.on_param)
      hp->callbacks.on_param(hp->callbacks.usrdata, sema_k->pos, sema_k->len, sema_v->pos, sema_v->len);
}

/******************************************************************************
 * "<c>"
 */
static bool hp_char(HeaderParser *hp, char c)
{
   Sema sema;
   Token *tok;

   sema_beg(&sema, hp);

   tok = hp_next(hp);

   return sema_end(&sema, tok->kind > TK_EOF && tok->c == c);
}

INLINE static char to_lower(char c)
{
   return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/******************************************************************************
 * "<s>" ; case-insensitive
 */
static bool hp_str(HeaderParser *hp, const char *s)
{
   Sema sema;
   Token *tok;

   sema_beg(&sema, hp);

   while (*s) {
      tok = hp_next(hp);
      if (tok->kind > TK_EOF && to_lower(tok->c) == to_lower(*s))
         s++;
      else
         break;
   }

   return sema_end(&sema, !*s);
}

static bool hp_kind(HeaderParser *hp, TokenKind kind)
{
   Sema sema;

   sema_beg(&sema, hp);

   return sema_end(&sema, hp_next(hp)->kind == kind);
}

typedef bool (*FnParse)(HeaderParser *hp);

/******************************************************************************
 * min*( fn_parse )
 */
static bool hp_repeat(HeaderParser *hp, size_t min, FnParse fn_parse)
{
   Sema sema;
   size_t count;

   sema_beg(&sema, hp);

   for (count = 0; fn_parse(hp); count++)
      ;

   return sema_end(&sema, count >= min);
}

/******************************************************************************
 * fld-vchar = OBS-TEXT / ALPHANUM / OCHAR
 */
static bool hp_fld_vchar(HeaderParser *hp)
{
   Sema sema;
   TokenKind kind;

   sema_beg(&sema, hp);

   kind = hp_next(hp)->kind;

   return sema_end(&sema, kind == TK_OBS_TEXT || kind == TK_ALPHANUM || kind == TK_OCHAR);
}

/******************************************************************************
 * quoted-pair = "\" ( WSP / fld-vchar )
 */
static bool hp_quoted_pair(HeaderParser *hp)
{
   Sema sema;

   sema_beg(&sema, hp);

   return sema_end(&sema, hp_char(hp, '\\') && (hp_kind(hp, TK_WSP) || hp_fld_vchar(hp)));
}

/******************************************************************************
 * qdtext = WSP / OBS-TEXT / %x21 / %x23-5B / %x5D-7E 
 * ; the ranges are fld-vchar except """ and "\"
 */
static bool hp_qdtext(HeaderParser *hp)
{
   Sema sema;
   Token *tok;

   sema_beg(&sema, hp);

   tok = hp_next(hp);

   return sema_end(
      &sema,
      tok->kind == TK_WSP || tok->kind == TK_OBS_TEXT || tok->kind == TK_ALPHANUM
         || (tok->kind == TK_OCHAR && !tok_strchr(tok, "\"\\"))
   );
}

/******************************************************************************
 * quoted-string-inner = qdtext / quoted-pair
 */
static bool hp_quoted_string_inner(HeaderParser *hp)
{
   return hp_qdtext(hp) || hp_quoted_pair(hp);
}

/******************************************************************************
 * quoted-val = DQUOTE *( quoted-string-inner ) DQUOTE
 */
static bool hp_quoted_val(HeaderParser *hp, Sema *sema)
{
   sema_beg(sema, hp);

   // it would be easy to include in the sema the part inside the quotes, but that would create a false sense of security.
   // it would still be necessary to use strncpy_unquote, so might as well do this
   return sema_end(
      sema,
      hp_kind(hp, TK_DQUOTE) && hp_repeat(hp, 0, hp_quoted_string_inner) && hp_kind(hp, TK_DQUOTE)
   );
}

/******************************************************************************
 * tchar = ALPHANUM / "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
 */
static bool hp_tchar(HeaderParser *hp)
{
   Sema sema;
   Token *tok;

   sema_beg(&sema, hp);

   tok = hp_next(hp);

   return sema_end(
      &sema,
      tok->kind == TK_ALPHANUM || (tok->kind == TK_OCHAR && tok_strchr(tok, "!#$%&'*+-.^_`|~"))
   );
}

/******************************************************************************
 * token = 1*tchar
 */
static bool hp_token(HeaderParser *hp)
{
   return hp_repeat(hp, 1, hp_tchar);
}

/******************************************************************************
 * unquoted-val = token
 */
static bool hp_unquoted_val(HeaderParser *hp, Sema *sema)
{
   sema_beg(sema, hp);

   return sema_end(sema, hp_token(hp));
}

/******************************************************************************
 * param-val = unquoted-val / quoted-val
 */
static bool hp_param_val(HeaderParser *hp, Sema *sema)
{
   return hp_unquoted_val(hp, sema) || hp_quoted_val(hp, sema);
}

/******************************************************************************
 * ows = *WSP
 */
static bool hp_ows(HeaderParser *hp)
{
   while (hp_kind(hp, TK_WSP))
      ;

   return true;
}

/******************************************************************************
 * param-name = token
 */
static bool hp_param_name(HeaderParser *hp, Sema *sema)
{
   sema_beg(sema, hp);

   return sema_end(sema, hp_token(hp));
}

/******************************************************************************
 * param = ows ";" ows [ param-name ows "=" ows param-val ]
 */
static bool hp_param(HeaderParser *hp)
{
   Sema sema, sema2, sema_k, sema_v;
   bool okay, okay2;

   sema_beg(&sema, hp);

   okay = hp_ows(hp) && hp_char(hp, ';') && hp_ows(hp);
   if (okay) {
      sema_beg(&sema2, hp);
      okay2 = sema_end(
         &sema2,
         hp_param_name(hp, &sema_k) && hp_ows(hp) && hp_char(hp, '=') && hp_ows(hp)
            && hp_param_val(hp, &sema_v)
      );
      if (okay2)
         hp_callback_param(hp, &sema_k, &sema_v);
   }

   return sema_end(&sema, okay);
}

/******************************************************************************
 * media-type = token "/" token
 */
static bool hp_media_type(HeaderParser *hp, Sema *sema)
{
   sema_beg(sema, hp);

   return sema_end(sema, hp_token(hp) && hp_char(hp, '/') && hp_token(hp));
}

/******************************************************************************
 * content-type-inner = media-type *fld-param
 */
static bool hp_content_type_inner(HeaderParser *hp)
{
   Sema sema, sema_val;
   bool okay;

   sema_beg(&sema, hp);

   okay = hp_media_type(hp, &sema_val);
   if (okay)
      hp_callback_val(hp, &sema_val);

   return sema_end(&sema, okay && hp_repeat(hp, 0, hp_param));
}

/******************************************************************************
 * ext-char = ALPHANUM / "!" / "#" / "$" / "%" / "&" / "+" / "-" / "." / "^" / "_" / "`" / "|" / "{" / "}" / "~"
 */
static bool hp_ext_char(HeaderParser *hp)
{
   Sema sema;
   Token *tok;

   sema_beg(&sema, hp);

   tok = hp_next(hp);

   return sema_end(
      &sema,
      tok->kind == TK_ALPHANUM || (tok->kind == TK_OCHAR && tok_strchr(tok, "!#$%&+-.^_`|{}~"))
   );
}

/******************************************************************************
 * ext-val = 1*ext-char
 * ; originally: charset  "'" [ language ] "'" value-chars
 * ; i don't need to parse it accurately, so i just expect a list of any of the possible chars i could get in the original
 */
static bool hp_ext_val(HeaderParser *hp, Sema *sema)
{
   sema_beg(sema, hp);

   return sema_end(sema, hp_repeat(hp, 1, hp_ext_char));
}

/******************************************************************************
 * disp-type = "inline" | "attachment" | token
 * ; case insensitive
 */
static bool hp_disp_type(HeaderParser *hp, Sema *sema)
{
   sema_beg(sema, hp);

   return sema_end(sema, hp_str(hp, "inline") || hp_str(hp, "attachment") || hp_token(hp));
}

/******************************************************************************
 * ext-token = 1*tchar "*"
 * ; "*" is included in tchar
 * ; there needs to be only one "*" at the end
 */
static bool hp_ext_token(HeaderParser *hp)
{
   Sema sema;
   size_t count = 0;
   bool okay = false;

   sema_beg(&sema, hp);

   while (hp_tchar(hp)) {
      if (hp->tok.c == '*') {
         okay = count > 0;
         break;
      }
      count++;
   }

   return sema_end(&sema, okay);
}

typedef bool (*FnParseSema)(HeaderParser *hp, Sema *sema);

/******************************************************************************
 * disp-name = token / ext-token
 */
static bool hp_disp_name(HeaderParser *hp, Sema *sema, FnParseSema *pfn_val)
{
   bool okay;

   sema_beg(sema, hp);

   okay = hp_ext_token(hp);
   if (okay)
      *pfn_val = hp_ext_val;
   else {
      okay = hp_token(hp);
      *pfn_val = hp_param_val;
   }

   return sema_end(sema, okay);
}

/******************************************************************************
 * disp-param = ows ";" ows [ ( ext-token ows "=" ows ext-val ) / ( token ows "=" ows param-val ) ]
 */
static bool hp_disp_param(HeaderParser *hp)
{
   Sema sema, sema2, sema_k, sema_v;
   FnParseSema fn_val;
   bool okay, okay2;

   sema_beg(&sema, hp);

   okay = hp_ows(hp) && hp_char(hp, ';') && hp_ows(hp);
   if (okay) {
      sema_beg(&sema2, hp);
      okay2 = sema_end(
         &sema2,
         hp_disp_name(hp, &sema_k, &fn_val) && hp_ows(hp) && hp_char(hp, '=') && hp_ows(hp)
            && fn_val(hp, &sema_v)
      );
      if (okay2)
         hp_callback_param(hp, &sema_k, &sema_v);
   }

   return sema_end(&sema, okay);
}

/******************************************************************************
 * content-disp-inner = disp-type *disp-param
 */
static bool hp_content_disp_inner(HeaderParser *hp)
{
   Sema sema, sema_val;
   bool okay;

   sema_beg(&sema, hp);

   okay = hp_disp_type(hp, &sema_val);
   if (okay)
      hp_callback_val(hp, &sema_val);

   return sema_end(&sema, okay && hp_repeat(hp, 0, hp_disp_param));
}

/******************************************************************************
 * content-type = ows val-content-type-inner
 */
const char *hp_content_type(const char *data, size_t len, HeaderCallbacks *callbacks)
{
   HeaderParser hp;
   bool okay;

   hp_init(&hp, data, len, callbacks);
   okay = hp_ows(&hp) && hp_content_type_inner(&hp);

   return okay ? hp.lx.pos : NULL;
}

/******************************************************************************
 * content-disp = ows val-content-disp-inner
 */
const char *hp_content_disp(const char *data, size_t len, HeaderCallbacks *callbacks)
{
   HeaderParser hp;
   bool okay;

   hp_init(&hp, data, len, callbacks);
   okay = hp_ows(&hp) && hp_content_disp_inner(&hp);

   return okay ? hp.lx.pos : NULL;
}

void strncpy_unquote(char *dst, const char *src, size_t len)
{
   if (*src == '"') {
      len -= 2;
      src++;
      while (len--) {
         if (*src == '\\')
            src++;
         *(dst++) = *(src++);
      }
      *dst = 0;
   }
   else {
      memcpy(dst, src, len);
      dst[len] = 0;
   }
}