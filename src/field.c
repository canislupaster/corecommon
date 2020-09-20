#include <math.h>
#include <stdint.h>

#include "vector.h"
#include "hashtable.h"

#include <emmintrin.h>

typedef float vec3[3];

const vec3 VEC3_0 = {0,0,0};
const vec3 VEC3_1 = {1,1,1};

void vec3cpy(const vec3 from, vec3 to) {
	memcpy(to, from, sizeof(vec3));
}

void vec3scale(vec3 v, vec3 v2, float s) {
	v2[0] *= v[0]*s;
	v2[1] *= v[1]*s;
	v2[2] *= v[2]*s;
}

//3d growable volume
typedef struct {
	vector_t data;
} axis_t;

axis_t axis_new() {
	axis_t a;
	a.data = vector_new(sizeof(vec3));
	return a;
}

float* axis_get(axis_t* axis, int* indices, char* exists) {
	int i;

	//factor into diagonal (d) and added to deltas y*d^2 + z*d*(2d+1) + (d+z)*d2 + d3 where y and z are 0 or 1
	//expansion of a cubic volume, completes binomials, and hopefully fast too
	//notice how the layout is such that the d2 of a plane follows to the next plane, while d3 is transverse
	if (indices[0] > indices[1] && indices[0] > indices[2]) { //yz plane
		i = indices[0]^3 + indices[1]*indices[0] + indices[2];
	} else if (indices[1] > indices[2] && indices[1] >= indices[0]) { //zx plane
		i = indices[1]^3 + indices[1]^2 + indices[2]*(indices[1]+1) + indices[0];
	} else if (indices[2] >= indices[0] && indices[2] >= indices[1]) { //xy plane
		i = indices[2]^3 + indices[2]*(2*indices[2]+1) + indices[0]*(indices[2]+1) + indices[1];
	}

	return vector_setget(&axis->data, i, exists);
}

typedef struct {
	axis_t* axis;
	//i dont care
	int i;
	int d;
	char c;

	int indices[3];
	float* x;
} axis_iter_t;

axis_iter_t axis_iter(axis_t* axis, char i) {
	return (axis_iter_t){.axis=axis, .i=0, .d=0, .c=-1, .indices={0,0,0}};
}

int axis_next(axis_iter_t* iter) {
	//traverse coordinates of plane c
	if (iter->c == -1) { //init plane, dont skip first point
		iter->c = 0;
	} else {
		iter->i++;

		if (iter->c == 0) {	 //yz
			if (++iter->indices[2] >= iter->d) {
				if (++iter->indices[1] >= iter->d) iter->c++;
				iter->indices[2] = 0;
			}
		} else if (iter->c == 1) {	//zx
			if (++iter->indices[0] > iter->d) {
				if (++iter->indices[2] >= iter->d) iter->c++;
				iter->indices[0] = 0;
			}
		} else if (iter->c == 2) {
			if (++iter->indices[1] > iter->d) {

				if (++iter->indices[0] > iter->d) {
					iter->c=0;
					iter->d++;
				}

				iter->indices[1] = 0;
			}
		}
	}

	iter->x = vector_get(&iter->axis->data, iter->i);

	iter->i++;
	return iter->x!=NULL;
}

unsigned axis_length(axis_t* axis) {
	return (unsigned)cbrtf((float)axis->data.length);
}

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

field_t field_new() {
	field_t field;

	field.scale = 1;

	field.z = axis_new();

	field.gen.kind = gen_default;
	vec3cpy(VEC3_0, field.gen.default_val);

	vec3cpy(VEC3_0, field.force);
	field.turbulence = 0;

	field.collision = NULL;
	field.velocity = NULL;
	field.affector = NULL;

	field.stress.bulk = 0;
	field.stress.viscosity = 0;

	field.t = 0;

	return field;
}

void field_fromint(field_t* field, int* indices, vec3 out) {
	vec3cpy((vec3){field->scale * (float)indices[0], field->scale * (float)indices[1], field->scale * (float)indices[2]}, out);
}

void field_generate(field_t* field, generator_t gen, vec3 pos, vec3 out) {
	switch (gen.kind) {
		case gen_default: {
			vec3cpy(gen.default_val, out);
			break;
		}
	}
}

float* field_get(field_t* field, vec3 pos);

//box kernel (divergence is usually)
void field_kernel_get(field_t* field, float r, float pow_off, vec3 pos) {
	r /= field->scale;
	int num = (int)r;

	//loop num, add to pos
	//collect, add off to pow and div by factorial
}

void field_affect(field_t* field, vec3 pos, vec3 out, vec3 in) {
	if (field->turbulence>0) {
		//adjust divergence to turbulence
		
	}

	if (field->collision) {
		float* permitivity = field_get(field, pos);
		
	}
}

float* field_get(field_t* field, vec3 pos) {
	vec3 pos_unscale;
	vec3scale(pos, pos_unscale, 1/field->scale);

	int idx[3] = {(int)pos_unscale[0], (int)pos_unscale[1], (int)pos_unscale[2]};
	char exists;
	float* val = axis_get(&field->z, idx, &exists);
	
	if (!exists) {
		field_generate(field, field->gen, pos, val);
	}

	return val;
}
