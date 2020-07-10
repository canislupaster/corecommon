// Automatically generated header.

#pragma once
#include <stdlib.h>
#include <string.h>
typedef struct {
	unsigned long size;

	unsigned long length;
	char* data;
} vector_t;
typedef struct {
	vector_t* vec;

	long i;
	char rev;
	void* x;
} vector_iterator;
vector_t vector_new(unsigned long size);
vector_t vector_from_string(char* str);
void vector_downsize(vector_t* vec);
void vector_upsize(vector_t* vec, unsigned long length);
void* vector_push(vector_t* vec);
void* vector_pushcpy(vector_t* vec, void* x);
void* vector_stock(vector_t* vec, unsigned long length);
void* vector_stockcpy(vector_t* vec, unsigned long length, void* data);
void* vector_stockstr(vector_t* vec, char* str);
unsigned long vector_elem(vector_t* vec, char* ptr);
void* vector_get(vector_t* vec, unsigned long i);
char* vector_getstr(vector_t* vec, unsigned long i);
void vector_truncate(vector_t* vec, unsigned long length);
int vector_pop(vector_t* vec);
char* vector_popptr(vector_t* vec);
void* vector_popcpy(vector_t* vec);
int vector_remove(vector_t* vec, unsigned long i);
char* vector_removeptr(vector_t* vec, unsigned long i);
int vector_removemany(vector_t* vec, unsigned long i, unsigned long len);
int vector_remove_element(vector_t* vec, char* x);
void* vector_insert(vector_t* vec, unsigned long i);
void* vector_insertcpy(vector_t* vec, unsigned long i, void* x);
void* vector_insert_many(vector_t* vec, unsigned long i, unsigned long length);
void* vector_insert_manycpy(vector_t* vec, unsigned long i, unsigned long length, void* x);
void* vector_insertstr(vector_t* vec, unsigned long i, char* str);
void* vector_set(vector_t* vec, unsigned long i);
void* vector_setcpy(vector_t* vec, unsigned long i, void* x);
vector_iterator vector_iterate(vector_t* vec);
vector_iterator vector_iterate_rev(vector_t* vec);
int vector_next(vector_iterator* iter);
void* vector_peek(vector_iterator* iter);
void vector_cpy(vector_t* from, vector_t* to);
void vector_add(vector_t* from, vector_t* to);
unsigned long vector_search(vector_t* vec, void* elem);
unsigned long vector_cmp(vector_t* vec1, vector_t* vec2);
unsigned long vector_cmpstr(vector_t* vec1, vector_t* vec2);
void vector_sort_inplace(vector_t* vec, size_t offset, size_t size);
vector_t vector_split_str(char* str, const char* delim);
vector_t vector_from_strings(char* start, unsigned long num);
void vector_flatten_strings(vector_t* vec, vector_t* out, char* delim, unsigned long len);
void vector_clear(vector_t* vec);
void vector_free(vector_t* vec);
void vector_expand_strings(vector_t* vec, vector_t* out, char* begin, char* delim, char* end);
void vector_free_strings(vector_t* vec);
