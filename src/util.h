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
void memcheck_init();
void drop(void* ptr);
trace stacktrace();
void print_trace(trace* trace);
void* heap(size_t size);
void* heapcpy(size_t size, const void* val);
char* heapstr(const char* fmt, ...);
void* resize(void* ptr, size_t size);
void memcheck();
