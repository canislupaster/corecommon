#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <execinfo.h>

#include "hashtable.h"
#include "str.h"

#define TRACE_SIZE 10

typedef struct {
	void* stack[TRACE_SIZE];
} trace;

static struct {
	map_t alloc_map;
	int initialized;
}
		ALLOCATIONS = {.initialized=0};

void memcheck_init() {
	if (!ALLOCATIONS.initialized) {
		ALLOCATIONS.alloc_map = map_new();
		map_configure_ptr_key(&ALLOCATIONS.alloc_map, sizeof(trace));
		ALLOCATIONS.initialized = 1;
	}
}

void drop(void* ptr) {
	free(ptr);
	if (ALLOCATIONS.initialized) map_remove(&ALLOCATIONS.alloc_map, &ptr);
}

trace stacktrace() {
	trace x = {};
	backtrace(x.stack, TRACE_SIZE);

	return x;
}

void print_trace(trace* trace) {
	printf("stack trace: \n");

	char** data = backtrace_symbols(trace->stack, TRACE_SIZE);

	for (int i = 0; i < TRACE_SIZE; i++) {
		printf("%s\n", data[i]);
	}

	drop(data);
}

void* heap(size_t size) {
	void* res = malloc(size);
	trace tr = stacktrace();

	if (!res) {
		fprintf(stderr, "out of memory!");
		print_trace(&tr);
		abort();
	}

	if (ALLOCATIONS.initialized) map_insertcpy(&ALLOCATIONS.alloc_map, &res, &tr);

	return res;
}

void* heapcpy(size_t size, const void* val) {
	void* res = heap(size);
	memcpy(res, val, size);
	return res;
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

void* resize(void* ptr, size_t size) {
	void* res = realloc(ptr, size);

	if (!res) {
		fprintf(stderr, "out of memory!");
		abort();
	}

	return res;
}

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