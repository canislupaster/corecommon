// Automatically generated header.

#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <execinfo.h>
#define TRACE_SIZE 10
typedef struct {
	void* stack[TRACE_SIZE];
} trace;
#if BUILD_DEBUG
void memcheck_init();
#endif
void drop(void* ptr);
trace stacktrace();
void print_trace(trace* trace);
void* heap(size_t size);
void* heapcpy(size_t size, const void* val);
char* heapcpystr(const char* str);
char* heapcpysubstr(const char* begin, size_t len);
char* heapstr(const char* fmt, ...);
int streq(char* str1, char* str2);
void* resize(void* ptr, size_t size);
char* read_file(char* path);
char *ext(char *filename);
#if BUILD_DEBUG
void memcheck();
#endif
char hexchar(char hex);
void charhex(unsigned char chr, char* out);
