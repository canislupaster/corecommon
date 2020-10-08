// Automatically generated header.

#pragma once
#include <string.h>
typedef struct {
	float* vals;
	int rows;
	int cols;
} mat_t;
float* mat_cell(mat_t* mat, int x, int y);
float mat_det(mat_t* mat);
