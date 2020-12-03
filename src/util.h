// Automatically generated header.

#pragma once
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#ifndef _WIN32
#include <execinfo.h>
#endif
#define TRACE_SIZE 10
typedef struct {
	void* stack[TRACE_SIZE];
} trace;
#if BUILD_DEBUG
void memcheck_init();
#endif
void drop(void* ptr);
#ifndef _WIN32
trace stacktrace();
#endif
#ifndef _WIN32
void print_trace(trace* trace);
#endif
void* heap(size_t size);
void* heapcpy(size_t size, const void* val);
char* heapcpystr(const char* str);
char* heapcpysubstr(const char* begin, size_t len);
char* heapstr(const char* fmt, ...);
char* stradd(char* str1, char* str2);
char* strpre(char* str, char* prefix);
char* straffix(char* str, char* affix);
int streq(char* str1, char* str2);
void* resize(void* ptr, size_t size);
char* getpath(char* path, char* parent);
char* read_file(char* path);
#ifndef _WIN32 //already defined
int max(int a, int b);
#endif
#ifndef _WIN32 //already defined
int min(int a, int b);
#endif
char* path_trunc(char* path, unsigned up);
char *ext(char *filename);
#ifdef BUILD_DEBUG
#ifndef _WIN32
void memcheck();
#endif
#endif
char hexchar(char hex);
void charhex(unsigned char chr, char* out);
void perrorx(char* err);
