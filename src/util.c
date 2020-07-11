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

#if BUILD_DEBUG
void memcheck_init() {
	if (!ALLOCATIONS.initialized) {
		ALLOCATIONS.alloc_map = map_new();
		map_configure_ptr_key(&ALLOCATIONS.alloc_map, sizeof(trace));
		map_distribute(&ALLOCATIONS.alloc_map);

		ALLOCATIONS.initialized = 1;
	}
}
#endif

void drop(void* ptr) {
#if BUILD_DEBUG
	if (ALLOCATIONS.initialized) map_remove(&ALLOCATIONS.alloc_map, &ptr);
#endif
	
	free(ptr);
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

	if (!res) {
		fprintf(stderr, "out of memory!");
		trace tr = stacktrace();
		print_trace(&tr);
		abort();
	}

#if BUILD_DEBUG
	if (ALLOCATIONS.initialized) {
		trace tr = stacktrace();
		map_insert_result mapres = map_insertcpy(&ALLOCATIONS.alloc_map, &res, &tr);
	}
#endif

	return res;
}

void* heapcpy(size_t size, const void* val) {
	void* res = heap(size);
	memcpy(res, val, size);
	return res;
}

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

void* resize(void* ptr, size_t size) {
	void* res = realloc(ptr, size);

	if (!res) {
		fprintf(stderr, "out of memory!");
		abort();
	}
	
	#if BUILD_DEBUG
		if (ALLOCATIONS.initialized && ptr != res) {
			map_remove(&ALLOCATIONS.alloc_map, &ptr);

			trace new_tr = stacktrace();
			map_insertcpy(&ALLOCATIONS.alloc_map, &res, &new_tr);
		}
	#endif

	return res;
}

//utility fn
char* read_file(char* path) {
	FILE *handle = fopen(path, "rb");
  if (!handle) {		
    return NULL;
  }

  fseek(handle, 0, SEEK_END);
  unsigned long len = ftell(handle);

  rewind(handle);

  char *str = heap(len + 1);

  fread(str, len, 1, handle);
  fclose(handle);

  str[len] = 0;

	return str;
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

#if BUILD_DEBUG
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
