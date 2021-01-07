// Automatically generated header.

#pragma once
#include <string.h>
typedef struct {
	float* vals;
	unsigned rows;
	unsigned cols;
} mat_t;
float* mat_cell(mat_t* mat, int x, int y);
void mat_mul(mat_t* a, mat_t* b, mat_t* out);
void mat_mul_vec3(mat_t* a, float* v, float* out);
typedef struct {
	float* vals;
	int diag;
	char lo;
} tri_mat_t;
tri_mat_t tri_mat_new(int diag, char lo);
float* tri_mat_cell(tri_mat_t* mat, int x, int y);
float* tri_mat_col(tri_mat_t* mat, int x);
float tri_mat_det(tri_mat_t* mat);
void tri_mat_inverse(tri_mat_t* mat, tri_mat_t* inverse);
void tri_mat_mul(tri_mat_t* a, tri_mat_t* b, mat_t* out);
float mat_det(mat_t* mat);
int mat_lu(mat_t* mat, tri_mat_t* l, tri_mat_t* u);
int mat_inverse_lu(mat_t* mat, mat_t* inverse, tri_mat_t* l, tri_mat_t* u);
int mat_inverse(mat_t* mat, mat_t* inverse);
