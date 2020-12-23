// Automatically generated header.

#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef enum {
	vector_cap = 0x1
} vector_flags_t;
typedef struct {
	vector_flags_t flags;

	unsigned size;
	unsigned length;

	char* data;
} vector_t;
typedef struct {
	vector_t vec;
	unsigned cap;
} vector_cap_t;
typedef struct {
	vector_t* vec;

	unsigned i;
	void* x;
} vector_iterator;
vector_t vector_new(unsigned size);
vector_cap_t vector_alloc(vector_t vec, unsigned cap);
vector_t vector_from_string(char* str);
void vector_downsize(vector_t* vec);
void vector_upsize(vector_t* vec, unsigned length);
static inline void* vector_push(vector_t* vec) {
	//allocate or resize
	vector_upsize(vec, 1);

	return vec->data + (vec->length - 1) * vec->size;
}
static inline void* vector_pushcpy(vector_t* vec, void* x) {
	void* pos = vector_push(vec);
	memcpy(pos, x, vec->size);
	return pos;
}
void* vector_stock(vector_t* vec, unsigned length);
void* vector_stockcpy(vector_t* vec, unsigned length, void* data);
void* vector_populate(vector_t* vec, unsigned length, void* item);
void* vector_stockstr(vector_t* vec, char* str);
unsigned vector_elem(vector_t* vec, char* ptr);
static inline void* vector_get(vector_t* vec, unsigned i) {
	if (i >= vec->length) {
		return NULL;
	}

	return vec->data + i * vec->size;
}
static inline char* vector_getstr(vector_t* vec, unsigned i) {
	char** x = vector_get(vec, i);
  return x ? *x : NULL;
}
void vector_truncate(vector_t* vec, unsigned length);
int vector_pop(vector_t* vec);
char* vector_popptr(vector_t* vec);
void* vector_popcpy(vector_t* vec);
int vector_remove(vector_t* vec, unsigned i);
void* vector_removeptr(vector_t* vec, unsigned i);
int vector_removemany(vector_t* vec, unsigned i, unsigned len);
int vector_remove_element(vector_t* vec, char* x);
void* vector_insert(vector_t* vec, unsigned i);
void* vector_insertcpy(vector_t* vec, unsigned i, void* x);
void* vector_insert_many(vector_t* vec, unsigned i, unsigned length);
void* vector_insert_manycpy(vector_t* vec, unsigned i, unsigned length, void* x);
void* vector_insertstr(vector_t* vec, unsigned i, char* str);
void* vector_set(vector_t* vec, unsigned i);
void* vector_setget(vector_t* vec, unsigned i, char* exists);
void* vector_setcpy(vector_t* vec, unsigned i, void* x);
static inline vector_iterator vector_iterate(vector_t* vec) {
	vector_iterator iter = {
			vec, .i=0
	};

	return iter;
}
vector_iterator vector_iterate_end(vector_t* vec);
static inline int vector_next(vector_iterator* iter) {
	iter->x = iter->vec->data + iter->i * iter->vec->size;
	iter->i++;

	return iter->i <= iter->vec->length;
}
int vector_prev(vector_iterator* iter);
int vector_skip(vector_iterator* iter, unsigned i);
void* vector_peek(vector_iterator* iter);
void vector_cpy(vector_t* from, vector_t* to);
void vector_add(vector_t* from, vector_t* to);
static inline unsigned vector_search(vector_t* vec, void* elem) {
	vector_iterator iter = vector_iterate(vec);
	while (vector_next(&iter)) {
		if (memcmp(iter.x, elem, vec->size)==0)
			return iter.i;
	}

	return 0;
}
int vector_search_remove(vector_t* vec, void* elem);
unsigned vector_cmp(vector_t* vec1, vector_t* vec2);
unsigned vector_cmpstr(vector_t* vec1, vector_t* vec2);
void vector_sort_inplace(vector_t* vec, size_t offset, size_t size);
vector_t vector_split_str(char* str, const char* delim);
vector_t vector_from_strings(char* start, unsigned num);
void vector_flatten_strings(vector_t* vec, vector_t* out, char* delim, unsigned len);
void vector_clear(vector_t* vec);
void vector_free(vector_t* vec);
void vector_expand_strings(vector_t* vec, vector_t* out, char* begin, char* delim, char* end);
void vector_free_strings(vector_t* vec);
