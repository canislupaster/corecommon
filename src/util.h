// Automatically generated header.

#pragma once
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <execinfo.h>
#define TRACE_SIZE 10
#define MIN(a, b) a < b ? a : b
#define MAX(a, b) a > b ? a : b
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
void* heapcpystr(const char* str);
char* heapstr(const char* fmt, ...);
void* resize(void* ptr, size_t size);
char* read_file(char* path);
char *ext(char *filename);
#if BUILD_DEBUG
void memcheck();
#endif
