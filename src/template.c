#include <stdlib.h>

#include "vector.h"
#include "util.h"

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

template_t template_new(char* data) {
	template_t template;

	//use same cursor method as used in percent parsing
	unsigned long template_len = strlen(data)+1;
	template.str = heap(template_len);
	memset(template.str, 0, template_len);

	template.skip = 0;
	template.max_args = 0;
	template.max_cond = 0;
	template.max_loop = 0;

	template.substitutions = vector_new(sizeof(substitution_t));

	char* write_cursor = template.str;
	char* read_cursor = data;

	while (*read_cursor) {
		if (strncmp(read_cursor, "!%", 2)==0) {
			if (read_cursor > data && *(read_cursor-1)=='!') {
				read_cursor++;
				*write_cursor = '%';
			} else {
				template.skip = (read_cursor+2)-data;
				break;
			}

		} else if (*read_cursor == '%') {
			read_cursor++;

			//escape
			if (*read_cursor == '%') {
				*write_cursor = '%';

				//condition
			} else if (*read_cursor == '!') {
				read_cursor++;

				substitution_t* cond = vector_push(&template.substitutions);
				cond->offset = write_cursor-template.str;

				cond->inverted = 0;
				cond->loop = 0;

				if (*read_cursor == '*') {
					cond->loop = 1;
					read_cursor++;
				} else if (*read_cursor == '!') {
					cond->inverted = 1;
					read_cursor++;
				}

				cond->idx = *read_cursor - '0';
				if (cond->loop && cond->idx >= template.max_loop)
					template.max_loop = cond->idx+1;
				else if (cond->idx >= template.max_cond)
					template.max_cond = cond->idx+1;

				char* cond_start = ++read_cursor;
				template_t insertion = template_new(cond_start);

				if (insertion.max_args > template.max_args)
					template.max_args = insertion.max_args;

				if (insertion.max_cond > template.max_cond)
					template.max_cond = insertion.max_cond;

				if (insertion.max_loop > template.max_loop)
					template.max_loop = insertion.max_loop;

				read_cursor += insertion.skip;

				cond->insertion = heapcpy(sizeof(template_t), &insertion);
				continue;

				//index subsitution
			} else {
				substitution_t sub = {.offset=write_cursor-template.str};

				if (*read_cursor == '#') {
					sub.noescape = 1;
					read_cursor++;
				} else {
					sub.noescape = 0;
				}

				sub.idx = *read_cursor - '0';
				if (sub.idx >= template.max_args) template.max_args = sub.idx+1;

				vector_pushcpy(&template.substitutions, &sub);

				read_cursor++;
				continue;
			}
		} else {
			*write_cursor = *read_cursor;
		}

		write_cursor++;
		read_cursor++;
	}

	return template;
}

void template_length(template_t* template, unsigned long* len, template_args* args) {
	*len += strlen(template->str);

	vector_iterator iter = vector_iterate(&template->substitutions);
	while (vector_next(&iter)) {
		substitution_t* sub = iter.x;
		if (sub->insertion) {
			if (sub->loop) {
				vector_iterator arg_iter = vector_iterate(args->loop_args[sub->idx]);
				while (vector_next(&arg_iter))
					template_length(sub->insertion, len, arg_iter.x);

				continue;
			}

			if (sub->inverted ^ !args->cond_args[sub->idx]) continue;
			template_length(sub->insertion, len, args);
		} else {
			char* arg = args->sub_args[sub->idx];

			if (sub->noescape) {
				*len += strlen(arg);
				continue;
			}

			while (*arg) {
				switch (*arg) {
					case '<': *len+=strlen("&lt;"); break;
					case '>': *len+=strlen("&gt;"); break;
					case '&': *len+=strlen("&amp;"); break;
					case '\'': *len+=strlen("&#39;"); break;
					case '"': *len+=strlen("&quot;"); break;
					default: (*len)++;
				}

				arg++;
			}
		}
	}
}

void template_substitute(template_t* template, char** out, template_args* args) {
	char* template_ptr = template->str;

	vector_iterator iter = vector_iterate(&template->substitutions);
	while (vector_next(&iter)) {
		substitution_t* sub = iter.x;

		unsigned long sub_before = sub->offset - (template_ptr - template->str);
		if (sub_before > 0) {
			memcpy(*out, template_ptr, sub_before);
			*out += sub_before;
			template_ptr += sub_before;
		}

		if (sub->insertion) {
			if (sub->loop) {
				vector_iterator arg_iter = vector_iterate(args->loop_args[sub->idx]);
				while (vector_next(&arg_iter)) {
					template_args* loop_args = arg_iter.x;
					template_substitute(sub->insertion, out, loop_args);

					//convenience free
					if (loop_args->sub_args)
						drop(loop_args->sub_args);
					if (loop_args->cond_args)
						drop(loop_args->cond_args);
					if (loop_args->loop_args)
						drop(loop_args->loop_args);
				}

				vector_free(args->loop_args[sub->idx]);

				continue;
			}

			if (sub->inverted ^ !args->cond_args[sub->idx]) continue;
			template_substitute(sub->insertion, out, args);
		} else {
			char* arg = args->sub_args[sub->idx];

			if (sub->noescape) {
				while (*arg) *((*out)++) = *(arg++);
				continue;
			}

			while (*arg) {
				char* escaped;

				switch (*arg) {
					case '<': escaped="&lt;"; break;
					case '>': escaped="&gt;"; break;
					case '&': escaped="&amp;"; break;
					case '"': escaped="&quot;"; break;
					case '\'': escaped="&#39;"; break;
					default: escaped=NULL;
				}

				if (escaped) {
					memcpy(*out, escaped, strlen(escaped));
					*out += strlen(escaped);
				} else {
					**out = *arg;
					(*out)++;
				}

				arg++;
			}
		}
	}

	unsigned long rest = strlen(template->str) - (template_ptr-template->str);
	memcpy(*out, template_ptr, rest);
	*out += rest;
}

char* do_template_va(template_t* template, va_list args) {
	//allocate arrays on stack, then reference
	int cond_args[template->max_cond];
	for (unsigned long i=0; i<template->max_cond; i++) {
		cond_args[i] = va_arg(args, int);
	}

	vector_t* loop_args[template->max_loop];
	for (unsigned long i=0; i<template->max_loop; i++) {
		loop_args[i] = va_arg(args, vector_t*);
	}

	char* sub_args[template->max_args];
	for (unsigned long i=0; i<template->max_args; i++) {
		sub_args[i] = va_arg(args, char*);
	}

	template_args t_args = {.cond_args=cond_args, .loop_args=loop_args, .sub_args=sub_args};

	unsigned long len = 0;
	template_length(template, &len, &t_args);

	char* out = heap(len+1);
	char* out_ptr = out;
	template_substitute(template, &out_ptr, &t_args);
	out[len] = 0;

	return out;
}