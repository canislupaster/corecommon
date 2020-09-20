// Automatically generated header.

#pragma once
#include <math.h>
#include <stdint.h>
#include <emmintrin.h>
#define MAX_DIM 3
typedef float vec3[3];
extern vec3 VEC3_0;
extern vec3 VEC3_1;
void vec3cpy(const vec3 from, vec3 to);
void vec3scale(vec3 v, vec3 v2, float s);
#include "vector.h"
typedef struct {
	vector_t data;
	vector_t neg_data;
} axis_t;
void axis_init(axis_t* axis, char i);
float* axis_get(axis_t* axis, int* indices, char i, char* exists);
typedef struct {
	axis_t* axis;
	char i;
	char init;
	int indices[MAX_DIM];
	axis_t* a[MAX_DIM];
	float* x;
} axis_iter_t;
axis_iter_t axis_iter(axis_t* axis, char i);
int axis_next(axis_iter_t* iter);
typedef struct {
	float bulk; //average uniaxial stress
	float viscosity; //stress multiplier of expansion
} stress_simple_t;
typedef struct {
	enum {
		gen_default
	} kind;

	union {
		vec3 default_val;
	};
} generator_t;
typedef struct field {
	axis_t z;

	float scale;

	generator_t gen;

	vec3 force;
	float turbulence; //uniaxial turbulence/diffusion multiplier

	//particle (in cell) physics
	struct field* collision; //field permitivity, affected by stress
	struct field* velocity;

	stress_simple_t stress;

	//other dynamics
	struct field* affector; //curl of (magnetic) field affected by electromotive force (every timestep). the flux is applied to the field

	float t;
} field_t;
field_t field_new();
void field_fromint(field_t* field, int* indices, vec3 out);
void field_generate(field_t* field, generator_t gen, vec3 pos, vec3 out);
float* field_get(field_t* field, vec3 pos);
void field_kernel_get(field_t* field, float r, float pow_off, vec3 pos);
void field_affect(field_t* field, vec3 pos, vec3 out, vec3 in);
float* field_get(field_t* field, vec3 pos);
