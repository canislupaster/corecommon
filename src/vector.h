// Automatically generated header.

#pragma once
#include <stdlib.h>
typedef struct {
	unsigned long size;

	unsigned long length;
	char* data;
} vector_t;
typedef struct {
	vector_t* vec;

	unsigned long i;
	char rev;
	void* x;
} vector_iterator;
vector_t vector_new(unsigned long size);
void vector_init(vector_t* vec, unsigned long length);
void* vector_push(vector_t* vec);
void* vector_pushcpy(vector_t* vec, void* x);
int vector_pop(vector_t* vec);
void* vector_popcpy(vector_t* vec);
int vector_remove(vector_t* vec, unsigned long i);
int vector_remove_element(vector_t* vec, char* x);
void* vector_get(vector_t* vec, unsigned long i);
void* vector_insert(vector_t* vec, unsigned long i);
void* vector_insertcpy(vector_t* vec, unsigned long i, void* x);
void* vector_set(vector_t* vec, unsigned long i);
void* vector_setcpy(vector_t* vec, unsigned long i, void* x);
vector_iterator vector_iterate(vector_t* vec);
int vector_next(vector_iterator* iter);
void vector_cpy(vector_t* from, vector_t* to);
void vector_clear(vector_t* vec);
void vector_free(vector_t* vec);
