// Automatically generated header.

#pragma once
#include <stdint.h>
#include <stdlib.h>
#define ROTATE(x, b) (uint64_t)( ((x) << (b)) | ( (x) >> (64 - (b))) )
#define HALF_ROUND(a, b, c, d, s, t)            \
    a += b; c += d;                \
    b = ROTATE(b, s) ^ a;            \
    d = ROTATE(d, t) ^ c;            \
    a = ROTATE(a, 32);
#define DOUBLE_ROUND(v0, v1, v2, v3)        \
    HALF_ROUND(v0,v1,v2,v3,13,16);        \
    HALF_ROUND(v2,v1,v0,v3,17,21);        \
    HALF_ROUND(v0,v1,v2,v3,13,16);        \
    HALF_ROUND(v2,v1,v0,v3,17,21);
uint64_t siphash24(const void* src, unsigned long src_sz, const char key[16]);
uint64_t siphash24_keyed(const void* src, unsigned long src_sz);
