#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define UTIL_TRACE
#endif

#ifdef UTIL_TRACE
#include <execinfo.h>
#endif

#include "hashtable.h"
#include "str.h"

#define TRACE_SIZE 10

typedef struct {
	void* stack[TRACE_SIZE];
} trace_t;

static struct {
	map_t alloc_map;
	int initialized;
}
		ALLOCATIONS = {.initialized=0};

#ifdef BUILD_DEBUG
void memcheck_init() {
	if (!ALLOCATIONS.initialized) {
		ALLOCATIONS.alloc_map = map_new();
		map_configure_ptr_key(&ALLOCATIONS.alloc_map, sizeof(trace_t));
		map_distribute(&ALLOCATIONS.alloc_map);

		ALLOCATIONS.initialized = 1;
	}
}
#endif

void drop(void* ptr) {
#ifdef BUILD_DEBUG
	if (ALLOCATIONS.initialized) map_remove(&ALLOCATIONS.alloc_map, &ptr);
#endif
	
	free(ptr);
}

#ifdef UTIL_TRACE
trace_t stacktrace() {
	trace_t x = { 0 };
	backtrace(x.stack, TRACE_SIZE);

	return x;
}

void print_trace(trace_t* trace_t) {
	printf("stack trace: \n");

	char** data = backtrace_symbols(trace_t->stack, TRACE_SIZE);

	for (int i = 0; i < TRACE_SIZE; i++) {
		printf("%s\n", data[i]);
	}

	drop(data);
}
#endif

void* heap(size_t size) {
	void* res = malloc(size);

	if (!res) {
		fprintf(stderr, "out of memory!");
#ifdef UTIL_TRACE
		trace_t tr = stacktrace();
		print_trace(&tr);
#endif
		abort();
	}

#ifdef BUILD_DEBUG
#ifdef UTIL_TRACE
	if (ALLOCATIONS.initialized) {
		trace_t tr = stacktrace();
		map_insert_result mapres = map_insertcpy(&ALLOCATIONS.alloc_map, &res, &tr);
	}
#endif
#endif

	return res;
}

void* heapcpy(size_t size, const void* val) {
	void* res = heap(size);
	memcpy(res, val, size);
	return res;
}

//strings

char* heapcpystr(const char* str) {
	return heapcpy(strlen(str) + 1, str);
}

char* heapcpysubstr(const char* begin, size_t len) {
	char* str = heap(len+1);
	str[len] = 0;
	memcpy(str, begin, len);
	return str;
}

/// asprintf but ignore errors and return string (or null if error)
char* heapstr(const char* fmt, ...) {
	char* strp = NULL;

	va_list ap;

	va_start(ap, fmt);
	vasprintf(&strp, fmt, ap);
	va_end(ap);

	return strp;
}

char* stradd(char* str1, char* str2) {
	char* s = heap(strlen(str1) + strlen(str2) + 1);
	memcpy(s, str1, strlen(str1));
	memcpy(s+strlen(str1), str2, strlen(str2)+1);
	return s;
}

char* strpre(char* str, char* prefix) {
	char* s = stradd(prefix, str);
	drop(str);
	return s;
}

char* straffix(char* str, char* affix) {
	char* s = stradd(str, affix);
	drop(str);
	return s;
}

//im tired of strcmp
int streq(char* str1, char* str2) {
	return strcmp(str1, str2)==0;
}

void* resize(void* ptr, size_t size) {
	void* res = realloc(ptr, size);

	if (!res) {
		fprintf(stderr, "out of memory!");
		abort();
	}

#ifdef BUILD_DEBUG
#ifdef UTIL_TRACE
		if (ALLOCATIONS.initialized && ptr != res) {
			map_remove(&ALLOCATIONS.alloc_map, &ptr);

			trace_t new_tr = stacktrace();
			map_insertcpy(&ALLOCATIONS.alloc_map, &res, &new_tr);
		}
#endif
#endif

	return res;
}

//utility fns
char* getpath(char* path) {
#if _WIN32
	char* new_path = malloc(1024);
	GetFullPathNameA(path, 1024, new_path, NULL);
#else
	char *new_path = realpath(path, NULL);
#endif

	return new_path;
}

char* read_file(char* path) {
	FILE *handle = fopen(path, "rb");
  if (!handle) {
    return NULL;
  }

  fseek(handle, 0, SEEK_END);
  unsigned len = ftell(handle);

  rewind(handle);

  char *str = heap(len + 1);

  fread(str, len, 1, handle);
  fclose(handle);

  str[len] = 0;

	return str;
}

#ifndef _WIN32 //already defined
int max(int a, int b) {
	return a>b?a:b;
}

int min(int a, int b) {
	return a>b?b:a;
}
#endif

char* path_trunc(char* path, unsigned up) {
	char* end = path + strlen(path);
	if (end == path) return path;

	while (up>0) {
		do end--; while (end>path && *end != '/');
		up--;
	}

	return heapcpysubstr(path, end-path);
}

//utility fn
char *ext(char *filename) {
  char *dot = "";
  for (; *filename; filename++) {
    if (*filename == '.')
      dot = filename;
  }

  return dot;
}

#ifdef BUILD_DEBUG
#ifdef UTIL_TRACE
void memcheck() {
	if (!ALLOCATIONS.initialized) return;

	if (ALLOCATIONS.alloc_map.length > 0) {
		fprintf(stderr, "memory leak detected\n");

		map_iterator iter = map_iterate(&ALLOCATIONS.alloc_map);
		while (map_next(&iter)) {
			printf("stacktrace for object at %ptr\n\n", *(void**) iter.key);
			print_trace(iter.x);
			printf("\n\n\n");
		}
	}

	map_free(&ALLOCATIONS.alloc_map);
	ALLOCATIONS.initialized = 0;
}
#endif
#endif

//kinda copied from https://nachtimwald.com/2017/09/24/hex-encode-and-decode-in-c/
//since im too lazy to type all these ifs
char hexchar(char hex) {
	if (hex >= '0' && hex <= '9') {
		return hex - '0';
	} else if (hex >= 'A' && hex <= 'F') {
		return hex - 'A' + 10;
	} else if (hex >= 'a' && hex <= 'f') {
		return hex - 'a' + 10;
	} else {
		return 0;
	}
}

void charhex(unsigned char chr, char* out) {
	char first = (char)(chr % 16u);
	if (first < 10) out[1] = first+'0';
	else out[1] = (first-10) + 'A';

	chr /= 16;

	if (chr < 10) out[0] = chr+'0';
	else out[0] = (chr-10) + 'A';
}

char B64_ALPHABET[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//designed for inefficiency and complexity
unsigned char* bit_reinterpret(unsigned char* src, unsigned inlen, unsigned* outlen, unsigned char inl, unsigned char outl) {
	*outlen = (inlen*inl)/outl + 1; //if aligned, remove padding
	unsigned char* out = (unsigned char*)heap(*outlen);

	unsigned char* srccur = src;
	unsigned char* cur = out;
	unsigned pos=0;

	*cur=0;

	while (1) {
		*cur |= (((*srccur<<(pos%inl))&(UCHAR_MAX<<(8-inl)))>>(pos%outl))&(UCHAR_MAX<<(8-outl));

		char incr;
		if (inl-(pos%inl) > outl-(pos%outl)) {
			incr = outl-(pos%outl);
		} else {
			incr = inl-(pos%inl);
		}

		if ((pos%outl) + incr >= outl) {
			cur++;
			*cur = 0;
		}

		if ((pos%inl) + incr >= inl) {
			srccur++;
			if (srccur-src >= inlen) {
				pos += incr;
				break;
			}
		}

		pos += incr;
	}

	*outlen = cur-out + (pos%outl > 0 ? 1 : 0);

	return out;
}

char* base64_encode(char* src, unsigned len) {
	unsigned olen;
	unsigned char* s = bit_reinterpret((unsigned char*)src, len, &olen, 8, 6);
	char* o = (char*)s;

	for (unsigned i=0; i<olen; i++) {
		o[i] = B64_ALPHABET[s[i]>>2];
	}

	o = resize(o, ((len+2)/3)*4 + 1);
	while ((olen*6)/24 != (len+2)/3) {
		o[olen++] = '=';
	}

	o[olen] = 0;

	return o;
}

//mods src
char* base64_decode(char* src, unsigned* len) {
	unsigned slen = strlen(src);
	unsigned len_off=0;
	while (slen>0 && src[slen-1]=='=') {
		len_off++;
		slen--;
	}

	for (unsigned i=0; i<slen; i++) {
		src[i] = (strchr(B64_ALPHABET, src[i])-B64_ALPHABET)<<2;
	}

	unsigned char* s = bit_reinterpret((unsigned char*)src, slen, len, 6, 8);
	*len -= len_off;
	return (char*)s;
}

void perrorx(char* err) {
	perror(err);
	exit(errno);
}

void errx(char* err) {
	fprintf(stderr, "%s\n", err);
	exit(0);
}