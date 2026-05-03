/*
 * Copyright (c) 2026 Alessandro Martone
 *
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef __HEADER_PARSER_H__
#define __HEADER_PARSER_H__

#include <stddef.h>

/**
 * @brief field main value
 */
typedef void (*ValCallback)(void *usrdata, const char *pos, size_t len);

/**
 * @brief field parameter
 * @p pos_v could be a quoted string, therefore must be manually unquoted with strncpy_unquote
 */
typedef void (*ParamCallback)(
   void *usrdata,
   const char *pos_k,
   size_t len_k,
   const char *pos_v,
   size_t len_v
);

typedef struct HeaderCallbacks {
   void *usrdata; /**< pointer to data used by the user */

   ValCallback on_val;     /**< callback for the main value of a field */
   ParamCallback on_param; /**< callback for every parameter of a field */
} HeaderCallbacks;

/**
 * @brief parse the value of the content-type header, in the scope of multipart requests
 *
 * @param[in] data      to the header field value (excluding field name and ':').
 * @param[in] len_data  length of @p data.
 * @param[in] callbacks user-provided callbacks invoked for the parsed value and parameters.
 * 
 * @note parsing is prefix-based: on success, the returned pointer indicates where parsing stopped.
 *       if it does not equal @p data + @p len_data, the remaining bytes were not recognized by
 *       this parser. whether that constitutes an error is left to the caller.
 * 
 * @return pointer to the first byte after the parsed field value, or NULL on parse failure.
 */
const char *hp_content_type(const char *data, size_t len_data, HeaderCallbacks *callbacks);

/**
 * @brief parse the value of the content-type header, in the scope of multipart requests
 *
 * @param[in] data      to the header field value (excluding field name and ':').
 * @param[in] len_data  length of @p data.
 * @param[in] callbacks user-provided callbacks invoked for the parsed value and parameters.
 * 
 * @note parsing is prefix-based: on success, the returned pointer indicates where parsing stopped.
 *       if it does not equal @p data + @p len_data, the remaining bytes were not recognized by
 *       this parser. whether that constitutes an error is left to the caller.
 * 
 * @return pointer to the first byte after the parsed field value, or NULL on parse failure.
 */
const char *hp_content_disp(const char *data, size_t len_data, HeaderCallbacks *callbacks);

/**
 * @brief create a null-terminated string from @p src , which is a parameter value and therefore could be quoted and have escape sequences
 *
 * @param[out] dst destination buffer at least long @p len + 1
 * @param[in]  src source
 * @param[in]  len length of @p src
 */
void strncpy_unquote(char *dst, const char *src, size_t len);

#endif /* __HEADER_PARSER_H__ */