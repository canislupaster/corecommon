// Automatically generated header.

#pragma once
#include <stdlib.h>
#include "vector.h"
typedef struct {
	char* str;
	unsigned long skip; //if a condition, length, including !%
	unsigned long max_args; //required arguments
	unsigned long max_cond;
	unsigned long max_loop;
	vector_t substitutions;
} template_t;
typedef struct {
	unsigned long idx;
	unsigned long offset;

	template_t* insertion;
	char inverted; //inverted condition
	char loop;
	char noescape;
} substitution_t;
typedef struct {
	int* cond_args;
	vector_t** loop_args;
	char** sub_args;
} template_args;
template_t template_new(char* data);
void template_length(template_t* template, unsigned long* len, template_args* args);
void template_substitute(template_t* template, char** out, template_args* args);
char* do_template_va(template_t* template, va_list args);
