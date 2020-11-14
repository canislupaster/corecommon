#include "field.h"
#include "vector.h"
#include "hashtable.h"

typedef struct {
	unsigned x, y;
	float v;
}	sparsemat_elem_t;

typedef struct {
	unsigned x; //+1, why do i keep doing this
	float v;
}	sparsemat_elem1_t;

typedef struct {
	vector_t elems;
	vector_t rows;
	vector_t cols;
} sparsemat_t;

void sparse_mul(sparsemat_t* mat, float* v, float* out) {
	for (unsigned i=0; i<mat->rows.length; i++) {
		sparsemat_elem1_t* e = &((sparsemat_elem1_t*)mat->rows.data)[i];
		float* vptr = v;

		if (e->x==0) continue;
		do out[i] += vptr[e->x]*e->v; while ((++e)->x != 0);
	}
}

void sparse_cgsolve(sparsemat_t* mat, float* in, float* buf, float* out) {
	for (unsigned i=0; i<mat->cols.length; i++) {

	}
}