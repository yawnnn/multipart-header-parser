#ifndef __MULTIPART_H__
#define __MULTIPART_H__

#include <stddef.h>

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

typedef struct ContentType {
   const char *val;
   const char *boundary;
} ContentType;

bool parse_content_type(const char *val, size_t len_val, ContentType *content_type);

typedef bool (*DataCallback)(void *usrdata, const char *pos, size_t len);
typedef bool (*CtrlCallback)(void *usrdata);

typedef struct MultipartCallbacks {
   void *usrdata;

   DataCallback on_fld_name;
   ValCallback on_fld_val;
   ParamCallback on_fld_param;
   DataCallback on_body;

   CtrlCallback on_part_beg;
   CtrlCallback on_header_end;
   CtrlCallback on_part_end;
   CtrlCallback on_end;
} MultipartCallbacks;

const char *
multipart_parse(const char *boundary, const char *data, size_t len_data, MultipartCallbacks *callbacks);

#endif /* __MULTIPART_H__ */