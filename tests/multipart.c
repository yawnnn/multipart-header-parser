#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "multipart.h"

#include "picohttpparser.h"

typedef struct phr_header phr_header;

#ifndef INLINE
   #ifdef _MSC_VER
      #define INLINE __inline
   #elif defined(__STDC__) && __STDC_VERSION__ >= 199901L
      #define INLINE inline
   #else
      #define INLINE
   #endif
#endif

typedef enum MultipartState {
   MPS_PREAMBLE = 0,  // RFC 2046 accepts a preamble
   MPS_FIRST_BOUNDARY,
   MPS_HEADERS_BEG,
   MPS_HEADERS,
   MPS_HEADERS_END,
   MPS_BODY,
   MPS_TRY_BODY_END,
   MPS_BODY_END,
   MPS_BOUNDARY,
   MPS_END,
   MPS_EPILOGUE,  // RFC 2046 accepts an epilogue
} MultipartState;

typedef struct ByteVec {
   char *ptr;
   size_t len;
   size_t cap;
} ByteVec;

typedef struct Multipart {
   MultipartState state;

   char *boundary;
   size_t len_boundary;
   size_t pos_boundary;

   const char *data;
   size_t len_data;

#define MAX_HEADERS 10
   phr_header headers[MAX_HEADERS];
   ByteVec bv;

   MultipartCallbacks callbacks;
} Multipart;

static void bytevec_truncate(ByteVec *bv, size_t len)
{
   if (len < bv->len)
      bv->len = len;
}

static void bytevec_push(ByteVec *bv, const char *buf, size_t len)
{
   size_t newlen = bv->len + len;

   if (bv->cap < newlen) {
      if (bv->cap)
         bv->ptr = realloc(bv->ptr, newlen);
      else
         bv->ptr = malloc(newlen);
      bv->cap = newlen;
   }

   memcpy(bv->ptr + bv->len, buf, len);
   bv->len = newlen;
}

static void bytevec_free(ByteVec *bv)
{
   free(bv->ptr);
   memset(bv, 0, sizeof(*bv));
}

static int sstricmp(const char *s1, size_t len1, const char *s2, size_t len2)
{
   int d;

   if (len1 != len2)
      return len1 > len2 ? 1 : -1;

   while (len1-- && (d = tolower(*s1) - tolower(*s2)) == 0) {
      s1++;
      s2++;
   }

   return d;
}

static int stricmp_with_ss(const char *s1, const char *s2, size_t len2)
{
   return sstricmp(s1, strlen(s1), s2, len2);
}

static char *strndup_unquote(const char *pos, size_t len)
{
   char *p = malloc(len + 1);
   strncpy_unquote(p, pos, len);
   return p;
}

static void
content_type_on_param(void *usrdata, const char *pos_k, size_t len_k, const char *pos_v, size_t len_v)
{
   ContentType *c_type = (ContentType *)usrdata;

   if (!stricmp_with_ss("boundary", pos_k, len_k))
      c_type->boundary = strndup_unquote(pos_v, len_v);
}

bool parse_content_type(const char *val, size_t len_val, ContentType *content_type)
{
   HeaderCallbacks callbacks = {0};

   memset(content_type, 0, sizeof(*content_type));

   callbacks.usrdata = content_type;
   callbacks.on_param = content_type_on_param;

   return hp_content_type(val, len_val, &callbacks) != NULL;
}

static void multipart_init(
   Multipart *mp,
   const char *boundary,
   const char *data,
   size_t len_data,
   MultipartCallbacks *callbacks
)
{
   memset(mp, 0, sizeof(*mp));

   mp->len_boundary = 2 + strlen(boundary);
   mp->boundary = (char *)malloc(mp->len_boundary + 1);
   strcpy(mp->boundary, "--");
   strcpy(mp->boundary + 2, boundary);

   mp->data = data;
   mp->len_data = len_data;

   memcpy(&mp->callbacks, callbacks, sizeof(*callbacks));
}

static void multipart_free(Multipart *mp)
{
   free(mp->boundary);
   bytevec_free(&mp->bv);
}

INLINE static bool run_data_callback(Multipart *mp, DataCallback callback, const char *pos, size_t len)
{
   if (!callback)
      return true;

   return callback(mp->callbacks.usrdata, pos, len);
}

INLINE static bool run_ctrl_callback(Multipart *mp, CtrlCallback callback)
{
   if (!callback)
      return true;

   return callback(mp->callbacks.usrdata);
}

static bool parse_fields(Multipart *mp, phr_header *headers, size_t nheaders)
{
   typedef const char *(*FnParse)(const char *, size_t, HeaderCallbacks *);

   FnParse fn_parse = NULL;
   HeaderCallbacks callbacks;
   phr_header *pheader;
   size_t i, i_end, j, len_val;
   const char *val;
   bool okay, found_ctype = false, found_cdisp = false;

   callbacks.usrdata = mp->callbacks.usrdata;
   callbacks.on_val = mp->callbacks.on_fld_val;
   callbacks.on_param = mp->callbacks.on_fld_param;

   /**
    * headers can be multiline with CRLF + WSP, like:
    * [0] -> name: "Content-Type" ; value: "application/json"
    * [1] -> name: NULL           ; value: "charset="US-ASCII""
    * ...
    * they should joined with whitespace
    * 
    * they can also appear multiple times
    * [0] -> name: "Content-Type" ; value: "application/json"
    * ...
    * [n] -> name: "Content-Type" ; value: "charset="US-ASCII""
    * AFAIK this mainly applies to list-like headers and therefore not relevant for what we need here, so i take the first and ignore the others
    */
   for (i = 0, okay = true; okay && i < nheaders; i++) {
      pheader = &headers[i];

      if (!pheader->name)
         continue;

      if (!found_ctype && !stricmp_with_ss("Content-Type", pheader->name, pheader->name_len)) {
         fn_parse = hp_content_type;
         found_ctype = true;
      }
      else if (!found_cdisp && !stricmp_with_ss("Content-Disposition", pheader->name, pheader->name_len)) {
         fn_parse = hp_content_disp;
         found_cdisp = true;
      }
      else
         continue;

      for (i_end = i + 1; i_end < nheaders; i_end++) {
         if (headers[i_end].name)
            break;
      }

      if (i_end - i > 1) {
         bytevec_truncate(&mp->bv, 0);
         for (j = i; j < i_end; j++) {
            if (j != i)
               bytevec_push(&mp->bv, " ", 1);
            bytevec_push(&mp->bv, headers[j].value, headers[j].value_len);
         }
         bytevec_push(&mp->bv, "", 1);

         val = mp->bv.ptr;
         len_val = mp->bv.len;
      }
      else {
         val = pheader->value;
         len_val = pheader->value_len;
      }

      if (mp->callbacks.on_fld_name)
         okay = mp->callbacks.on_fld_name(mp->callbacks.usrdata, pheader->name, pheader->name_len);

      if (okay)
         okay = fn_parse(val, len_val, &callbacks) != NULL;
   }

   return okay;
}

static bool multipart_parse_headers(Multipart *mp, const char **ppos, size_t len)
{
   size_t nheaders = MAX_HEADERS;
   int nread;

   nread = phr_parse_headers(*ppos, len, mp->headers, &nheaders, 0);
   if (nread < 0)
      return false;

   if (!parse_fields(mp, mp->headers, nheaders))
      return false;

   *ppos += nread;

   return true;
}

static const char *multipart_parse_body(Multipart *mp)
{
   size_t len_body = 0;
   uint8_t incr;
   const char *pos, *end, *beg_body = NULL;
   bool okay, newl;

   pos = mp->data;
   end = mp->data + mp->len_data;

   for (; pos < end; pos += incr) {
      incr = 1;
      newl = false;

      if (*pos == '\n')
         newl = true;
      else if (*pos == '\r' && pos + 1 < end && *(pos + 1) == '\n') {
         newl = true;
         incr = 2;
      }

      switch (mp->state) {
      case MPS_PREAMBLE:
         if (*pos != '-')
            break;

         mp->state = MPS_FIRST_BOUNDARY;
      // fall-through
      case MPS_FIRST_BOUNDARY:
         if (mp->pos_boundary == mp->len_boundary) {
            if (newl) {
               mp->pos_boundary = 0;
               mp->state = MPS_HEADERS_BEG;
            }
            else
               return pos;
         }
         else if (*pos == mp->boundary[mp->pos_boundary])
            mp->pos_boundary++;
         else
            return pos;
         break;

      case MPS_HEADERS_BEG:
         okay = run_ctrl_callback(mp, mp->callbacks.on_part_beg);
         if (!okay)
            return pos;

         mp->state = MPS_HEADERS;
      // fall-through
      case MPS_HEADERS:
         okay = multipart_parse_headers(mp, &pos, (size_t)(end - pos));
         if (!okay)
            return pos;

         mp->state = MPS_HEADERS_END;
      // fall-through
      case MPS_HEADERS_END:
         okay = run_ctrl_callback(mp, mp->callbacks.on_header_end);
         if (!okay)
            return pos;

         beg_body = pos;
         mp->state = MPS_BODY;
      // fall-through
      case MPS_BODY:
         // here it needs to be exactly CRLF is accepted
         if (newl && *pos == '\r') {
            len_body = (size_t)(pos - beg_body);
            mp->pos_boundary = 0;
            mp->state = MPS_TRY_BODY_END;
         }
         break;

      case MPS_TRY_BODY_END:
         if (*pos == mp->boundary[mp->pos_boundary]) {
            mp->pos_boundary++;
            if (mp->pos_boundary == mp->len_boundary)
               mp->state = MPS_BODY_END;
         }
         else {
            incr = 0;
            mp->state = MPS_BODY;
         }
         break;

      case MPS_BODY_END:
         okay = run_data_callback(mp, mp->callbacks.on_body, beg_body, len_body);
         if (!okay)
            return pos;

         okay = run_ctrl_callback(mp, mp->callbacks.on_part_end);
         if (!okay)
            return pos;

         mp->state = MPS_BOUNDARY;
      // fall-through
      case MPS_BOUNDARY:
         if (*pos == '-')
            mp->state = MPS_END;
         else if (newl)
            mp->state = MPS_HEADERS_BEG;
         break;

      case MPS_END:
         if (*pos == '-') {
            okay = run_ctrl_callback(mp, mp->callbacks.on_end);
            if (!okay)
               return pos;

            mp->state = MPS_EPILOGUE;
         }
         else
            return pos;
         break;

      case MPS_EPILOGUE:
         break;

      default:
         return pos;
         break;
      }
   }

   return pos;
}

const char *
multipart_parse(const char *boundary, const char *data, size_t len_data, MultipartCallbacks *callbacks)
{
   Multipart mp;
   const char *pos;

   multipart_init(&mp, boundary, data, len_data, callbacks);
   pos = multipart_parse_body(&mp);
   multipart_free(&mp);

   return pos;
}