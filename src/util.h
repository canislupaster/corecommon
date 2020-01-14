#pragma once
#include <stdlib.h>

#include <stdio.h>

#include <string.h>

#include <stdarg.h>

#include <execinfo.h>

void drop(void* ptr);
void* heap(size_t size);
void* heapcpy(size_t size, const void* val);
char* heapstr(const char* fmt, ...);
void* resize(void* ptr, size_t size);
void memcheck();
