/* Minimal JSON string-building helpers shared across the daemon. */
#ifndef SPKR_JSONUTIL_H
#define SPKR_JSONUTIL_H

#include <stddef.h>

void json_escape(const char *in, char *out, size_t out_len);
int json_str_or_null(char *dst, size_t cap, const char *val);

#endif
