#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "hashtable.h"

//configuration file parser

int ws(char** s) { return **s == '\n' || **s == '\t' || **s == ' '; }

void skip_ws(char** s) {
  while (**s && ws(s)) (*s)++;
}

int skip_char(char** s, char x) {
  if (**s == x) { (*s)++; return 1; } else return 0;
}

int skip_while(char** s, char* x) {
	while (strchr(x, **s)) {
		if (!**s) return 0;
		(*s)++;
	}

	return 1;
}

int skip_until(char** s, char* x) {
	while (!strchr(x, **s)) {
		if (!**s) return 0;
		(*s)++;
	}

	return 1;
}

int skip_name(char** s, char* name) {
  if (strncmp(*s, name, strlen(name))==0) {
    *s += strlen(name);
    return 1;
  } else {
    return 0;
  }
}

char* parse_name(char** s) {
	if (!**s || ws(s)) return 0;

	char* nmstart = *s;

	while (**s && !ws(s)) {
		(*s)++;
	}

	char* name = heap(*s - nmstart + 1);
	name[*s - nmstart] = 0;

	strncpy(name, nmstart, *s - nmstart);

	(*s)++;

	return name;
}

char* parse_string(char** s) {
  if (**s == '\"') {
    (*s)++;
    char* strstart = *s;

    while (**s != '\"' && **s) {
      if (**s == '\\') {
        (*s)++;
      }

      (*s)++;
    }

    char* str = heap(*s - strstart + 1);
    str[*s - strstart] = 0;

    strncpy(str, strstart, *s - strstart);

    (*s)++;

    return str;
  } else {
    return NULL;
  }
}

int parse_num(char** s, int* parsed) {
	int succ = sscanf(*s, "%i", parsed);

	skip_until(s, "\n");
	return succ;
}

int parse_float(char** s, double* parsed) {
	int succ = sscanf(*s, "%lf\n", parsed);
	
	skip_until(s, "\n");
	return succ;
}

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

char* cfgdir() {
	return getenv("HOME");
}

void configure(map_t* default_cfg, char* file) {
	char* path = heapstr("%s/%s", cfgdir(), file);
  char* cfg = read_file(path); drop(path);
  if (!cfg) return;

  char* cfgc = cfg;

	while (1) {
		skip_ws(&cfgc);
		if (!*cfgc) break;
		
		char* name = parse_name(&cfgc);

		config_val* val = map_find(default_cfg, &name);

		if (!val) {
			fprintf(stderr, "cannot parse config, no value for %s", name);
			return;
		}

		val->is_default = 0;

		skip_ws(&cfgc);
		if (!skip_char(&cfgc, '=')) {
			fprintf(stderr, "cannot parse config, expected = at byte/char %li", cfgc - cfg);
		}

		skip_ws(&cfgc);

		int succ=1;
		switch (val->ty) {
			case cfg_num: succ=parse_num(&cfgc, &val->data.num); break;
			case cfg_float: succ=parse_float(&cfgc, &val->data.float_); break;
			case cfg_str: {
				val->data.str = parse_string(&cfgc);
				succ = val->data.str ? 1 : 0;
			}
		}

		if (!succ) fprintf(stderr, "invalid value provided for %s in config", name);
	}
}

//saves and frees config
void save_configure(map_t* cfg, char* file) {
	char* path = heapstr("%s/%s", cfgdir(), file);
	FILE* f = fopen(path, "w"); drop(path);

	if (!f) return;
	
	map_iterator iter = map_iterate(cfg);
	while (map_next(&iter)) {
		config_val* val = iter.x;
		if (val->is_default) continue;

		fprintf(f, "%s = ", *(char**)iter.key);

		switch (val->ty) {
			case cfg_num: fprintf(f, "%i", val->data.num); break;
			case cfg_float: fprintf(f, "%lf", val->data.float_); break;
			case cfg_str: {
				fprintf(f, "%s", val->data.str);
				drop(val->data.str);
				break;
			}
		}

		fprintf(f, "\n");
	}

	fclose(f);
}

void cfg_add(map_t* cfg, const char* name, cfg_ty ty, cfg_data data) {
	map_insertcpy(cfg, &name, &(config_val){.ty=ty, .data=data, .is_default=1});
}

int* cfg_get(map_t* cfg, const char* name) {
	config_val* val = map_find(cfg, &name);
	return &val->data.num;
}

int* cfg_val(map_t* cfg, const char* name) {
	config_val* val = map_find(cfg, &name);
	val->is_default = 0;
	return &val->data.num;
}
