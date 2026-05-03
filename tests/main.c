#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>

#include "multipart.h"

void panic(const char *msg)
{
   fprintf(stderr, "%s\n", msg);
   exit(1);
}

static char *strndup_unquote(const char *pos, size_t len)
{
   char *p = malloc(len + 1);
   strncpy_unquote(p, pos, len);
   return p;
}

#define dbg_d(pos, len) __dbg(__func__, pos, len)
#define dbg_c()         __dbg(__func__, NULL, 0)

static void __dbg(const char *func, const char *pos, size_t len)
{
   if (pos) {
      char *p = strndup_unquote((const char *)pos, len);
      printf("-(%s): '%s'\n", func + 3, p);
      free(p);
   }
   else
      printf("*(%s)\n", func + 3);
}

static bool on_fld_name(void *usrdata, const char *pos, size_t len)
{
   dbg_d(pos, len);
   return true;
}

static void on_fld_val(void *usrdata, const char *pos, size_t len)
{
   dbg_d(pos, len);
}

static void on_fld_param(void *usrdata, const char *pos_k, size_t len_k, const char *pos_v, size_t len_v)
{
   dbg_d(pos_k, len_k);
   dbg_d(pos_v, len_v);
}

static bool on_body(void *usrdaa, const char *pos, size_t len)
{
   dbg_d(pos, len);
   return true;
}

static bool on_part_beg(void *usrdata)
{
   dbg_c();
   return true;
}

static bool on_header_end(void *usrdata)
{
   dbg_c();
   return true;
}

static bool on_part_end(void *usrdata)
{
   dbg_c();
   return true;
}

static bool on_end(void *usrdata)
{
   dbg_c();
   return true;
}

int main()
{
   const char *req = "\
POST / HTTP/1.1\r\n\
Host: localhost:8080\r\n\
User-Agent: curl/8.17.0\r\n\
Accept: */*\r\n\
Content-Length: 484\r\n\
Content-Type: multipart/form-data; boundary=X-BOUNDARY-123456\r\n\r\n\
--X-BOUNDARY-123456\r\n\
Content-Disposition: form-data;\r\n\
  name=\"username\"\r\n\
\r\n\
alice\r\n\
--X-BOUNDARY-123456\r\n\
Content-Disposition: form-data; name=\"metadata\"\r\n\
Content-Type: application/json; charset=\"US-ASCII\"\r\n\
\r\n\
{\"role\":\"tester\",\"active\":true}\r\n\
--X-BOUNDARY-123456\r\n\
Content-Disposition: form-data; name=\"file\"; filename=\"test.txt\"\r\n\
Content-Type: text/plain\r\n\
\r\n\
Hello world\r\n\
\r\n\
--X-BOUNDARY-123456--";

   ContentType c_type = {0};
   MultipartCallbacks callbacks = {0};

   callbacks.on_fld_name = on_fld_name;
   callbacks.on_fld_val = on_fld_val;
   callbacks.on_fld_param = on_fld_param;
   callbacks.on_body = on_body;
   callbacks.on_part_beg = on_part_beg;
   callbacks.on_header_end = on_header_end;
   callbacks.on_part_end = on_part_end;
   callbacks.on_end = on_end;

   const char *data = req;
   const char *end = req + strlen(req);

   const char *pdata = strstr(data, "Content-Type:");
   if (!pdata)
      panic("Content-Type not found");
   pdata += strlen("Content-Type:");

   bool okay = parse_content_type(pdata, (size_t)(end - pdata), &c_type);
   if (!okay || !c_type.boundary)
      panic("couldn't find boundary");

   pdata = strstr(pdata, "\r\n\r\n");
   if (!pdata)
      panic("Couldn't find header's end");
   pdata += 4;

   const char *pos = multipart_parse(c_type.boundary, pdata, (size_t)(end - pdata), &callbacks);
   if (pos != end) {
      char msg[256];
      sprintf(msg, "pos: %d\n%s\n", (int)(pos - data), pos);
      panic(msg);
   }
}