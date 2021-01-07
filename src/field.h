// Automatically generated header.

#pragma once
#include <math.h>
#include <stdint.h>
typedef float vec3[3];
extern const vec3 VEC3_0;
extern const vec3 VEC3_1;
void vec3cpy(const vec3 from, vec3 to);
void vec3scale(vec3 v, float s, vec3 out);
void vec3cross(vec3 v1, vec3 v2, vec3 out);
void vec3add(vec3 v1, vec3 v2, vec3 out);
void vec3sub(vec3 v1, vec3 v2, vec3 out);
void vec3mul(vec3 v1, vec3 v2, vec3 out);
void vec3div(vec3 v1, vec3 v2, vec3 out);
void vec3normalize(vec3 v, vec3 out);
float vec3mag(vec3 v);
float vec3sum(vec3 v);
float vec3dot(vec3 v1, vec3 v2);
int vec3eq(vec3 v1, vec3 v2);
void vec3abs(vec3 v, vec3 out);
void vec3neg(vec3 v, vec3 out);
float vec_dot(float* v1, float* v2, unsigned len);
#include "vector.h"
typedef struct {
	vector_t data;
} axis_t;
axis_t axis_new();
float* axis_get(axis_t* axis, int* indices, char* exists);
typedef struct {
	axis_t* axis;
	//i dont care
	int i;
	int d;
	char c;

	int indices[3];
	float* x;
} axis_iter_t;
axis_iter_t axis_iter(axis_t* axis);
int axis_next(axis_iter_t* iter);
unsigned axis_length(axis_t* axis);
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
	float turbulence; //turbulence/diffusion multiplier

	//particle (in cell) physics
	struct field* collision; //field permitivity, affected by stress
	struct field* acceleration; //base field

	stress_simple_t stress; //viscosity

	//other dynamics
	//struct field* affector; //curl of (magnetic) field affected by electromotive force (every timestep). the flux is applied to the field
} field_t;
field_t field_new();
void field_fromint(field_t* field, int* indices, vec3 out);
void field_generate(field_t* field, generator_t gen, vec3 pos, vec3 out);
void field_getd(field_t* field, float r, int pow_off, vec3 pos, vec3 out);
float* field_get(field_t* field, vec3 pos);