#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <Shlobj.h>
#endif

#include "util.h"
#include "hashtable.h"

//configuration file parser

int ws(char* s) { return *s == '\n' || *s == '\r' || *s == '\t' || *s == ' '; }

void skip_ws(char** s) {
  while (**s && ws(*s)) (*s)++;
}

int skip_char(char** s, char x) {
  if (**s == x) { (*s)++; return 1; } else return 0;
}

int skip_while(char** s, char* x) {
	int ret;
	while (strchr(x, **s)) {
		(*s)++;
		ret=1;
	}

	return ret;
}

int skip_any(char** s, char* x) {
	if (strchr(x, **s)) { (*s)++; return 1; } else return 0;
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
	if (!**s || ws(*s)) return 0;

	char* nmstart = *s;

	while (**s && !ws(*s)) {
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
	char* start = *s;
	*parsed = (int)strtol(*s, s, 10);
	return *s!=start;
}

int parse_float(char** s, double* parsed) {
	char* start = *s;
	*parsed = strtod(*s, s);
	return start!=*s;
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
#ifdef _WIN32
	WCHAR path[MAX_PATH];
	if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path))) {
		errx("couldnt get profile");
	}

	char* spath = heap(MAX_PATH);
	wcstombs(spath, path, MAX_PATH);
	return spath;
#else
	return getenv("HOME");
#endif
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
				succ = val->data.str!=NULL;
			}
		}

		if (!succ) fprintf(stderr, "invalid value provided for %s in config", name);
	}
}

//saves cfg
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
				fprintf(f, "\"%s\"", val->data.str);
				break;
			}
		}

		fprintf(f, "\n");
	}

	fclose(f);
}

void cfg_free(map_t* cfg) {
	map_iterator iter = map_iterate(cfg);
	while (map_next(&iter)) {
		config_val* val = iter.x;
		if (val->is_default) continue;

		switch (val->ty) {
			case cfg_str: {
				drop(val->data.str);
				break;
			}
			default:;
		}
	}

	map_free(cfg);
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
