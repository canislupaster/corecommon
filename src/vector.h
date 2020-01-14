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
void* vector_push(vector_t* vec);
void* vector_pushcpy(vector_t* vec, void* x);
int vector_pop(vector_t* vec);
void* vector_get(vector_t* vec, unsigned long i);
void* vector_setcpy(vector_t* vec, unsigned long i, void* x);
vector_iterator vector_iterate(vector_t* vec);
int vector_next(vector_iterator* iter);
void vector_cpy(vector_t* from, vector_t* to);
void vector_clear(vector_t* vec);
void vector_free(vector_t* vec);
