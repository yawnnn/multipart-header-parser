### HeaderParser

HeaderParser is a **zero-copy HTTP header value parser** focused specifically on the needs of **multipart requests**.

It parses and splits the values and parameters of the `Content-Type` and `Content-Disposition` headers, to the extent required and permitted by the multipart specifications (see *Limitations*).

The parser operates on raw header values, follows the formal grammar defined in the relevant RFCs, and reports parsed components through user-provided callbacks.

---

### Why

I could not find a small, self-contained C library that correctly parses HTTP header *values* with formal grammar.

There are many libraries that:

* parse multipart bodies
* split HTTP headers into name/value pairs

But very few that parse **header values themselves**, even though each header has its own syntax and grammar. 
In practice this leaves users with two choices: write an ad-hoc parser or using bigger libraries, which can be a hard sell.

Since the grammar for these headers is well-defined, and I needed correct behavior, I implemented a dedicated parser that's up to spec.

---

### Limitations

* This parser is intentionally limited to `Content-Type` and `Content-Disposition`, which are the headers relevant to multipart requests.
* Extended parameters (e.g. `filename*`) are recognized and passed verbatim to the caller, but are **not decoded**. Formally, such parameters are not allowed in multipart, so decoding is intentionally omitted.
* Only the **header field value** is parsed. Parsing of header field names and multipart bodies is left to the user (an example is provided in `tests/`).
* The focus is on correctness and conformance, not performance.

---

### Usage

```c
typedef struct ContentType {
   const char *val;
   size_t len_val;
   const char *charset;
   size_t len_charset;
} ContentType;

static void content_type_on_val(void *usrdata, const char *pos, size_t len)
{
   ContentType *c_type = (ContentType *)usrdata;

   c_type->val = pos;
   c_type->len_val = len;
}

static void content_type_on_param(
   void *usrdata,
   const char *pos_k, size_t len_k,
   const char *pos_v, size_t len_v)
{
   ContentType *c_type = (ContentType *)usrdata;

   if (len_k == strlen("charset") && !strncasecmp("charset", pos_k, len_k)) {
      c_type->charset = pos_v;
      c_type->len_charset = len_v;
   }
}

bool parse_content_type(const char *val, size_t len_val, ContentType *c_type)
{
   HeaderCallbacks callbacks = {0};

   memset(c_type, 0, sizeof(*c_type));

   callbacks.usrdata = c_type;
   callbacks.on_val = content_type_on_val;
   callbacks.on_param = content_type_on_param;

   return hp_content_type(val, len_val, &callbacks) != NULL;
}
```