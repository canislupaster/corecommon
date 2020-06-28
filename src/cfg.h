// Automatically generated header.

#pragma once
#include <string.h>
#include <stdlib.h>
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
void cfg_add(map_t* cfg, const char* name, cfg_ty ty, cfg_data data);
int* cfg_get(map_t* cfg, const char* name);
int* cfg_val(map_t* cfg, const char* name);
