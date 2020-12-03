// Automatically generated header.

#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#if (defined(__APPLE__)) && !((defined(_WIN32)) && !(defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
#  include <libkern/OSByteOrder.h>
#endif
#if !((defined(__APPLE__)) && !((defined(_WIN32)) && !(defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)))
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#    include <sys/endian.h>
#endif
#if !(defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__))
#    include "endian.h"
#endif
#endif
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#  define _le64toh(x) ((uint64_t)(x))
#endif
#if (defined(_WIN32)) && !(defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#  define _le64toh(x) ((uint64_t)(x))
#endif
#if (defined(__APPLE__)) && !((defined(_WIN32)) && !(defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
#  define _le64toh(x) OSSwapLittleToHostInt64(x)
#endif
#if !((defined(__APPLE__)) && !((defined(_WIN32)) && !(defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)))
#if defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && \
	__BYTE_ORDER == __LITTLE_ENDIAN
#    define _le64toh(x) ((uint64_t)(x))
#endif
#if !(defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && \
	__BYTE_ORDER == __LITTLE_ENDIAN)
#    define _le64toh(x) le64toh(x)
#endif
#endif
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
uint64_t siphash24(const void* src, unsigned src_sz, const char key[16]);
uint64_t siphash24_keyed(const void* src, unsigned src_sz);
