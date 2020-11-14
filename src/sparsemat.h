// Automatically generated header.

#pragma once
typedef struct {
	unsigned x, y;
	float v;
}	sparsemat_elem_t;
typedef struct {
	unsigned x; //+1, why do i keep doing this
	float v;
}	sparsemat_elem1_t;
#include "vector.h"
typedef struct {
	vector_t elems;
	vector_t rows;
	vector_t cols;
} sparsemat_t;
void sparse_mul(sparsemat_t* mat, float* v, float* out);
void sparse_cgsolve(sparsemat_t* mat, float* in, float* buf, float* out);
