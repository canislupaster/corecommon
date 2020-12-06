// Automatically generated header.

#pragma once
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <Shlobj.h>
#endif
int ws(char* s);
void skip_ws(char** s);
int skip_char(char** s, char x);
int skip_while(char** s, char* x);
int skip_any(char** s, char* x);
int skip_until(char** s, char* x);
int skip_name(char** s, char* name);
char* parse_name(char** s);
char* parse_string(char** s);
int parse_num(char** s, int* parsed);
int parse_float(char** s, double* parsed);
typedef enum {
	cfg_num, cfg_float, cfg_str
} cfg_ty;
typedef union {
	int num;
	double float_;
	char* str;
} cfg_data;
typedef struct {
  cfg_ty ty;
  cfg_data data;
	int is_default;
} config_val;
char* cfgdir();
#include "hashtable.h"
void configure(map_t* default_cfg, char* file);
void save_configure(map_t* cfg, char* file);
void cfg_free(map_t* cfg);
void cfg_add(map_t* cfg, const char* name, cfg_ty ty, cfg_data data);
int* cfg_get(map_t* cfg, const char* name);
int* cfg_val(map_t* cfg, const char* name);
