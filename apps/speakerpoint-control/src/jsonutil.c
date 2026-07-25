#include "jsonutil.h"

#include <stdio.h>

void json_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (; in && *in && o + 2 < out_len; in++) {
        unsigned char ch = (unsigned char)*in;
        if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = (char)ch; }
        else if (ch >= 0x20) out[o++] = (char)ch;
    }
    out[o] = '\0';
}

int json_str_or_null(char *dst, size_t cap, const char *val)
{
    if (!val || !*val) return snprintf(dst, cap, "null");
    char esc[1024];
    json_escape(val, esc, sizeof(esc));
    return snprintf(dst, cap, "\"%s\"", esc);
}
