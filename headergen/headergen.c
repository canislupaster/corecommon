//i just realized this is terrible ill probably rewrite it when i get bored enough

#include <stdio.h>
#include <stdlib.h>

#define MIN(a, b) (a < b ? a : b)

#include "../src/hashtable.h"
#include "../src/tinydir.h"
#include "../src/util.h"
#include "../src/vector.h"

typedef struct {
  enum { if_none, if_def, if_ndef } kind;
  char *cond;
} if_t;

typedef struct {
  struct file *file;
  unsigned object_id;  // indexes into sorted_objects
  char *declaration;
  vector_t deps;  // char*
  vector_t include_deps;

  vector_t ifs;

  int pub;
} object_t;

typedef struct {
	char* raw;
	char* path; // resolved paths for lookup
	vector_t ifs;

	int pub;
} include;

typedef struct file {
  map_t objects;  // map of vectors (for aliasing) of objects
  vector_t deps;  // file-wide deps from contents of functions/globals

  vector_t sorted_objects;
  vector_t includes;

  int pub;  // has any published objects
} file;

typedef struct {
  int braces;
  int parens;

  char *path;
  char *filestart;
  char *file;
  vector_t if_stack;  // stack of #ifs to copy to objects

  file *current;

  map_t files;

  vector_t deps;
} state;

object_t*object_new(state *state_) {
	object_t*obj = heap(sizeof(object_t));
  obj->object_id = state_->current->sorted_objects.length;

  obj->file = state_->current;
  obj->deps = vector_new(sizeof(char *));
  obj->include_deps = vector_new(sizeof(include*));
  obj->pub = 0;

  vector_cpy(&state_->if_stack, &obj->ifs);

  return obj;
}

void object_ref(state *state_, char *name, object_t*obj) {
  map_insert_result res = map_insert(&state_->current->objects, &name);
  vector_t *vec = res.val;

  if (!res.exists) {
    *vec = vector_new(sizeof(object_t*));
  }

  vector_pushcpy(vec, &obj);
}

void object_push(state *state_, char *name, object_t*obj) {
  vector_pushcpy(&state_->current->sorted_objects, &obj);
  object_ref(state_, name, obj);
}

typedef struct {
  char *start;
  int len;
} token;

// please dont read my shitty parser read the real one instead this one is
// really fucking bad

char *SKIP = "!*=<>/-+&[],.:|&%^@";
// braindump, just add more if it breaks
char *SKIP_WORDS[] = {"extern", "case",  "switch", "while", "for",
                      "do",     "const", "if",     "else",  "return"};
char *SKIP_TYPES[] = {"int", "char", "long", "double", "float", "void"};

int ws(state *state) {
  return *state->file == ' ' || *state->file == '\r' || *state->file == '\n' ||
         *state->file == '\t';
}

int parse_comment(state *state) {
  // skip comments
	if (strlen(state->file) < 2) return 0;

  if (strncmp(state->file, "//", 2) == 0) {
    state->file += 2;  // skip //
    while (*state->file && *state->file != '\n') state->file++;

    return 1;
  } else if (strncmp(state->file, "/*", 2) == 0) {
    state->file += 2;  // skip /*
    while (*state->file && strncmp(state->file, "*/", 2) != 0) state->file++;
    state->file += 2;  // skip */

    return 1;
  } else
    return 0;
}

int skip_ws_comment(state *state) {
	int skipped=0;
  while (ws(state)) {
  	state->file++;
  	skipped=1;
  }

  if (parse_comment(state)) {
		skip_ws_comment(state);
  	skipped=1;
  }

  return skipped;
}

void parse_define(state *state) {
  while (*state->file != '\n' && *state->file && !parse_comment(state)) {
    if (*state->file == '\\') state->file++;
    state->file++;
  }
}

int skip_word(state *state, char *word) {
  if (strncmp(word, state->file, strlen(word)) == 0) {
    char *start = state->file;
    state->file += strlen(word);

    if (ws(state) || !*state->file || strchr(SKIP, *state->file) ||
        strchr("{}", *state->file))
      return 1;
    else
      state->file = start;  // no skip or brace or ws after word, not parsed
  }

  return 0;
}

int skip_list(state *state, char **skiplist, size_t size) {
  for (int i = 0; i < size / sizeof(char *); i++) {
    if (skip_word(state, skiplist[i])) return 1;
  }

  return 0;
}

int skip_nows(state *state) {
  // skip strings
  if (*state->file == '\"') {
    state->file++;  // skip "
    while (*state->file != '\"' && *state->file) {
      if (*state->file == '\\') state->file++;
      state->file++;
    }

    if (*state->file) state->file++;  // skip "

    return 1;
  }

  if (*state->file == '\'') {
    state->file++;  // skip '
    if (*state->file == '\\') state->file++;
		if (*state->file) state->file++;  // skip char
    if (*state->file) state->file++;  // skip '

    return 1;
  }

  // skip reserved tokens or numbers
  if (*state->file &&
      (strchr("0123456789", *state->file) || strchr(SKIP, *state->file))) {
    state->file++;
    return 1;
  }

  if (skip_list(state, SKIP_WORDS, sizeof(SKIP_WORDS))) {
    return 1;
  }

  return 0;
}

int skip(state *state) {
	int skipped=0;
	while (skip_ws_comment(state) || skip_nows(state)) {
		skipped=1;
	}

	return skipped;
}

int skip_sep(state *state) {
  if (*state->file == ';') {
    state->file++;
    return 1;
  } else
    return 0;
}

int parse_start_paren(state *state) {
  if (!*state->file) return 0;

  if (*state->file == '(' && state->parens == 0) {
    state->file++;
    return 1;
  } else
    return 0;
}

int parse_end_paren(state *state) {
  if (!*state->file) return 1;

  if (*state->file == ')') {
    state->file++;
    if (state->parens == 0) {
      return 1;
    } else {
      state->braces--;
      return parse_end_paren(state);
    }
  } else {
    return 0;
	}
}

int skip_paren(state *state) {
  if (*state->file == '(') {
    state->parens++;
    state->file++;
    return 1;
  } else if (*state->file == ')') {
    state->parens--;
    state->file++;
    return 1;
  } else {
    return 0;
	}
}

int parse_start_brace(state *state) {
	skip(state);

  if (*state->file == '{' && state->braces <= 0) {
    state->file++;
    return 1;
  } else
    return 0;
}

int parse_end_brace(state *state) {
	skip(state);

  if (!*state->file) return 1;

  if (*state->file == '}') {
    state->file++;
    if (state->braces == 0) {
      return 1;
    } else {
      state->braces--;
      return parse_end_brace(state);
    }
  } else if (*state->file == '{') {
    state->braces++;
    state->file++;

    return parse_end_brace(state);
  } else
    return 0;
}

int skip_braces(state *state) {
  if (*state->file == '{') {
    state->braces++;
    state->file++;
    return 1;
  } else if (*state->file == '}') {
    state->braces--;
    state->file++;
    return 1;
  } else
    return 0;
}

int parse_sep(state *state) {
  return *state->file == ';';
}

/// does a crappy check to see if previous token was probably a name
int parse_name(state *state) {
	skip_ws_comment(state);
  if (!*state->file) return 0;

  if (strchr("[(=,", *state->file))
    return 1;
  else
    return 0;
}

token parse_token_noskip(state *state) {
  if (!*state->file) return (token){.start=NULL, .len=0};

  token tok;
  tok.start = state->file;
  tok.len = 0;

  while (!ws(state) && *state->file && !strchr(SKIP, *state->file) &&
         !strchr("{}();", *state->file)) {
    tok.len++;
    state->file++;
  }

  return tok;
}

/// parses identifiers
token parse_token(state *state) {
	skip(state);

  return parse_token_noskip(state);
}

token parse_token_braced(state *state) {
	skip(state);
  if (skip_sep(state) || skip_paren(state) || skip_braces(state)) {
    return parse_token_braced(state);
  }

  return parse_token_noskip(state);
}

int token_eq(token tok, char *str) {
  return tok.len == strlen(str) && strncmp(tok.start, str, tok.len) == 0;
}

char *range(char *start, char *end) {
  char *str = heap(end - start + 1);
  memcpy(str, start, end - start);
  str[end - start] = 0;
  return str;
}

char *token_str(state *state, token tok) {
  if (!tok.len) {
    fprintf(stderr, "expected token at %s... in %s",
            range(state->file, state->file + MIN(20, strlen(state->file))), state->path);

    exit(1);
  }

  char *str = heap(tok.len + 1);
  memcpy(str, tok.start, tok.len);
  str[tok.len] = 0;
  return str;
}

char *dirname(char *filename) {
  char *slash = NULL;

  for (char *fnptr = filename; *fnptr; fnptr++) {
    if (*fnptr == '/')
      slash = fnptr + 1;  // include slash
    else if (*fnptr == '\\' && *fnptr + 1 == '\\')
      slash = fnptr + 2;
  }

  if (!slash) return "";

  char *new_path = heap(slash - filename + 1);
  memcpy(new_path, filename, slash - filename);
  new_path[slash - filename] = 0;

  return new_path;
}

token parse_string(state *state) {
	skip_ws_comment(state);

  if (*state->file == '\"') {
    token tok;

    state->file++;
    tok.start = state->file;
    tok.len = 0;

    while (*state->file != '\"' && *state->file) {
      if (*state->file == '\\') {
        state->file++;
        tok.len++;
      }

      state->file++;
      tok.len++;
    }

    state->file++;
    return tok;
  } else {
    return (token){.start=NULL, .len=0};
  }
}

object_t*parse_object(state *state, object_t*super, int static_);

int skip_type(state *state) {
  if (skip_word(state, "unsigned")) {
		skip_ws_comment(state);
    skip_list(state, SKIP_TYPES, sizeof(SKIP_TYPES));
    return 1;
  } else if (skip_list(state, SKIP_TYPES, sizeof(SKIP_TYPES))) {
    return 1;
  } else {
    return 0;
  }
}

object_t* parse_object(state *state, object_t*super, int static_) {
  if (skip_type(state)) {
    return NULL;
  }

	object_t*obj = NULL;
  char *name = NULL;

  token first = parse_token_braced(state);

  if (!first.len) return NULL;
  char *start = first.start;

  if (token_eq(first, "typedef")) {
    obj = object_new(state);
    parse_object(state, obj, static_);  // parse thing inside

    name = token_str(state, parse_token(state));

    skip(state);
    parse_sep(state);
    obj->declaration = straffix(range(start, state->file), ";");

    skip_sep(state);
  } else if (token_eq(first, "struct") || token_eq(first, "union") || token_eq(first, "enum")) {
    char *kind_prefix;

    if (token_eq(first, "struct")) kind_prefix = "struct ";
    else if (token_eq(first, "union")) kind_prefix = "union ";
		else if (token_eq(first, "enum")) kind_prefix = "enum ";
		else return NULL;

    token name_tok = {.start=NULL, .len=0};
    int paren = state->parens;
    while (*state->file && *state->file != ';' && *state->file != ',') {
      if (parse_start_brace(state)) {  // otherwise fwd decl
        obj = super ? super
                    : object_new(state);  // used so lower-order objects
                                          // reference most superior object
				if (token_eq(first, "enum")) {
					while (!parse_end_brace(state)) {
						char *str = token_str(state, parse_token(state));
						object_ref(state, str, obj);
					}
				} else {
					while (!parse_end_brace(state)) {
						parse_object(state, obj, static_);  // parse field type, add deps
						skip(state);
						if (!parse_sep(state)) parse_token(state); // parse field name or dont (ex. anonymous union)

						while (!skip_sep(state)) { // parse any addendums (ex. function pointer paren)
							skip(state);
							if (skip_paren(state)) continue;
							if (skip_sep(state)) break;
							parse_object(state, obj, 1);
						}
					}
				}

        if (!super) obj->declaration = straffix(range(start, state->file), ";");

        break;
      }

      name_tok = parse_token(state);
			// attributes sometimes have parentheses, but dont skip parens of a global object (end paren of function args)
      if (*state->file == ')' && state->parens==paren) break;
      else skip_paren(state);
    }

    if (name_tok.len) {
      name = heapstr("%s %s;", kind_prefix, token_str(state, name_tok));
    }
  } else {
    char *str = token_str(state, first);

    if (super) vector_pushcpy(&super->deps, &str);
    vector_pushcpy(&state->deps, &str);
    return NULL;
  }

  // superior object means that there is already an object that encapsulates
  // this one
  if (obj && name && !super && !static_) {
    object_push(state, name, obj);
  }

  if (obj) {
    return obj;
  }

  return NULL;
}

char *get_cond(state *state) {
	skip_ws_comment(state);
  char *ifstart = state->file;

  parse_define(state);

  return range(ifstart, state->file);
}

include* get_inc(file* f, char* str, vector_t* ifs) {
	vector_iterator iter = vector_iterate(&f->includes);
	while (vector_next(&iter)) {
		include* inc = *(include**)iter.x;
		if (ifs->length == inc->ifs.length && streq(str, inc->path)) {
			vector_iterator iter2 = vector_iterate(ifs);
			int br=0;
			while (vector_next(&iter2)) {
				if_t* other = vector_get(&inc->ifs, iter2.i-1);
				if (!streq(((if_t*)iter2.x)->cond, other->cond)) {
					br=1;
					break;
				}
			}

			if (br) break;
			else return inc;
		}
	}

	return NULL;
}

object_t* parse_global_object(state *state) {
	skip(state);
  if (skip_sep(state)) return NULL;

	char *start = state->file;

	if (*state->file=='#') {
		state->file++;
		skip_ws_comment(state);
		if (skip_word(state, "include")) {
			skip_ws_comment(state);
			if (*state->file != '"') {
				char* path_start = state->file;

				parse_define(state);
				char* str = range(start, state->file);
				char* path = range(path_start, state->file);

				if (!get_inc(state->current, path, &state->if_stack)) {
					include* inc = heapcpy(sizeof(include), &(include){.raw=str, .path=path, .pub=1});
					vector_cpy(&state->if_stack, &inc->ifs);

					vector_pushcpy(&state->current->includes, &inc);
				}
			} else {
				char* path =
						strpre(token_str(state, parse_string(state)), dirname(state->path));

				path[strlen(path) - 1] = 'c';  //.h -> .c for finds and gen

				char* str = range(start, state->file);

				char* rpath = getpath(path, state->path);

				include* inc = heapcpy(sizeof(include), &(include){.path=rpath ? rpath : path, .raw=str, .pub=rpath == NULL});
				vector_cpy(&state->if_stack, &inc->ifs);

				if (!get_inc(state->current, inc->path, &state->if_stack))
					vector_pushcpy(&state->current->includes, &inc);
			}
		} else if (skip_word(state, "if")) {
			vector_pushcpy(&state->if_stack,
										 &(if_t){.kind = if_none, .cond = get_cond(state)});
		} else if (skip_word(state, "ifdef")) {
			vector_pushcpy(&state->if_stack,
										 &(if_t){.kind = if_def, .cond = get_cond(state)});
		} else if (skip_word(state, "ifndef")) {
			vector_pushcpy(&state->if_stack,
										 &(if_t){.kind = if_ndef, .cond = get_cond(state)});
		} else if (skip_word(state, "elif")) {
			if_t* cond = vector_get(&state->if_stack, state->if_stack.length - 1);
			if_t new_cond = {.kind=if_none};
			new_cond.cond = heapstr("(%s) && !(%s)", get_cond(state), cond->cond);

			vector_pop(&state->if_stack);                 // pop old if
			vector_pushcpy(&state->if_stack, &new_cond);  // pop new else
		} else if (skip_word(state, "else")) {
			if_t* cond = vector_get(&state->if_stack, state->if_stack.length - 1);
			if_t new_cond;
			switch (cond->kind) {
				case if_none: {
					new_cond.kind = if_none;
					new_cond.cond = heapstr("!(%s)", cond->cond);

					break;
				}
				case if_def: {
					new_cond.kind = if_ndef;
					new_cond.cond = cond->cond;
					break;
				}
				case if_ndef: {
					new_cond.kind = if_def;
					new_cond.cond = cond->cond;
					break;
				}
			}

			vector_pop(&state->if_stack);
			vector_pushcpy(&state->if_stack, &new_cond);
		} else if (skip_word(state, "endif")) {
			vector_pop(&state->if_stack);
		} else if (skip_word(state, "define")) {
			object_t* obj = object_new(state);
			char* name = token_str(state, parse_token(state));

			// holy fuck
			while (*state->file && *state->file != '\n') {
				if (*state->file == '\\') {
					state->file++;
					if (*state->file == '\r') state->file++;
					if (*state->file == '\n') state->file++;
					continue;
				}

				if (skip_nows(state) || skip_sep(state) || skip_paren(state) ||
						skip_braces(state))
					continue;

				// avoid newlines
				if (*state->file == ' ' || *state->file == '\t' || *state->file == '\r') {
					state->file++;
					continue;
				}

				token tok = parse_token_noskip(state);
				char *str = token_str(state, tok);

				vector_pushcpy(&state->current->deps, &str);
				vector_pushcpy(&obj->deps, &str);
			}

			obj->declaration = range(start, state->file);  // no semicolon for defines
			object_push(state, name, obj);
		} else { // all other directives
			parse_define(state);
		}

		return NULL;
	}

	// reset deps (instead of allocating a new object and
	// using super, we can just use a dep buffer in state)
  vector_clear(&state->deps);

	int static_ = skip_word(state, "static");
	skip_ws_comment(state);
	int inline_ = skip_word(state, "inline");

	object_t* ty = parse_object(state, NULL, static_ && !inline_);
  char *reset = state->file;  // reset to ty if not a global variable/function

  token name_tok = parse_token(state);
  if (!name_tok.len) return ty;

  char *name = token_str(state, name_tok);

  char *after_name = state->file;
	skip_ws_comment(state);

	object_t* obj;

  if (parse_sep(state) || *state->file == '=') {  // global
		obj = object_new(state);
		vector_cpy(&state->deps, &obj->deps);

		obj->declaration = heapstr("extern %s;", range(start, after_name));

		while (!parse_sep(state)) {
			if (skip_braces(state) || skip_paren(state) || skip_type(state) || skip(state)) continue;
			char *str = token_str(state, parse_token_braced(state));
			vector_pushcpy(&state->current->deps, &str);
		}

		skip_sep(state);
	} else if (parse_start_paren(state)) {  // function
		obj = object_new(state);
		vector_cpy(&state->deps, &obj->deps);

		while (!parse_end_paren(state)) {
			if (skip(state)) continue;

			if (!skip_type(state)) parse_object(state, obj, static_);

			if (parse_start_paren(state)) {
				parse_token(state);
				parse_end_paren(state);

				if (parse_start_paren(state)) {
					while (!parse_end_paren(state)) {
						parse_object(state, obj, static_);
					}
				}
			} else if (!parse_name(state)) {
				parse_token(state);  // parse argument name
			}
		}

		if (!inline_) obj->declaration = straffix(range(start, state->file), ";");

		int braces=0;

		if (parse_start_brace(state)) {
			while (!parse_end_brace(state)) {
				if (skip(state) || skip_paren(state) || skip_type(state) || skip_braces(state)) continue;

				if (strncmp(state->file, "#if", strlen("#if"))==0) { //true story
					braces = state->braces;
				} else if (strncmp(state->file, "#elif", strlen("#elif"))==0) {
					//restore braces from preceding #if
					state->braces = braces;
				}

				if (parse_end_brace(state)) break;
				if (skip_sep(state)) continue;  // handle preamble to another statement

				token tok = parse_token_braced(state);
				char *str = token_str(state, tok);
				vector_pushcpy(&state->current->deps, &str);
			}
		}

		if (inline_) obj->declaration = range(start, state->file);
	} else {
    state->file = reset;
    return ty;
  }

	//assumed static inline is public
  if (!static_ || inline_) object_push(state, name, obj);
  return obj;
}

void pub_deps(file *file, object_t*obj) {
  if (obj->pub) return;  // already marked
  obj->pub = 1;

  vector_iterator dep_iter = vector_iterate(&obj->deps);
  while (vector_next(&dep_iter)) {
    vector_t *objs_dep = map_find(&file->objects, dep_iter.x);
    if (!objs_dep) continue;

    vector_iterator iter_objs = vector_iterate(objs_dep);
    while (vector_next(&iter_objs)) {
      pub_deps(file, *(object_t**)iter_objs.x);
    }
  }
}

void set_ifs(file* gen_this, FILE* handle, vector_t* if_stack, vector_t* new_ifs) {
	// remove old ifs
	int remove_to = 0;

	vector_iterator file_if_iter = vector_iterate(if_stack);
	while (vector_next(&file_if_iter)) {
		if_t *file_if = file_if_iter.x;
		if_t *obj_if = vector_get(new_ifs, file_if_iter.i - 1);

		if (!obj_if || !streq(obj_if->cond, file_if->cond) ||
				obj_if->kind != file_if->kind) {
			remove_to = file_if_iter.i - 1;
			break;
		}
	}

	for (int i = if_stack->length - remove_to; i > 0; i--) {
		vector_pop(if_stack);
		fprintf(handle, "#endif\n");
	}

	vector_iterator if_iter = vector_iterate(new_ifs);
	if_iter.i = remove_to;  // add from point at which stack is divergent

	while (vector_next(&if_iter)) {
		if_t *_if = if_iter.x;

		switch (_if->kind) {
			case if_none:
				fprintf(handle, "#if %s\n", _if->cond);
				break;
			case if_def:
				fprintf(handle, "#ifdef %s\n", _if->cond);
				break;
			case if_ndef:
				fprintf(handle, "#ifndef %s\n", _if->cond);
				break;
		}

		vector_pushcpy(if_stack, if_iter.x);
	}
}

void add_file(state *state, char *path, char *filename) {
  if (!streq(ext(filename), ".c")) return;
  path = getpath(path, NULL);

  if (!path) {
    fprintf(stderr, "%s does not exist", filename);
    return;
  }

  FILE *handle = fopen(path, "rb");
  if (!handle) {
    fprintf(stderr, "cannot read %s", filename);
    free(path);
    return;
  }

  // pretty much copied from frontend
  fseek(handle, 0, SEEK_END);
  unsigned len = ftell(handle);

  rewind(handle);

  char *str = heap(len + 1);

  fread(str, len, 1, handle);
  fclose(handle);

  str[len] = 0;

  file *new_file = map_insert(&state->files, &path).val;

  new_file->objects = map_new();
  map_configure_string_key(&new_file->objects, sizeof(vector_t));
  new_file->sorted_objects = vector_new(sizeof(object_t*));

  new_file->deps = vector_new(sizeof(char *));

  new_file->pub = 0;
  new_file->includes = vector_new(sizeof(include*));

  state->filestart = str;
  state->file = str;
  state->path = path;

  state->braces = 0;
  state->parens = 0;

  state->if_stack = vector_new(sizeof(if_t));

  state->current = new_file;

  while (*state->file) {
    parse_global_object(state);
  }

  drop(str);
}

void recurse_add_file(state *state, tinydir_file file) {
  if (file.is_dir) {
    tinydir_dir dir;
    tinydir_open(&dir, file.path);

    for (; dir.has_next; tinydir_next(&dir)) {
      tinydir_readfile(&dir, &file);
			if (streq(file.name, ".") || streq(file.name, "..") || streq(file.name, "./"))
				continue;

      recurse_add_file(state, file);
    }

    tinydir_close(&dir);
  } else {
    add_file(state, file.path, file.name);
  }
}

int main(int argc, char **argv) {
  state state_ = {.files = map_new(), .deps = vector_new(sizeof(char *))};
  map_configure_string_key(&state_.files, sizeof(file));

  int pub = 0;  //--pub makes everything public

  // PARSE STEP: GO THROUGH ALL FILES AND PARSE OBJECTS N STUFF
  for (int i = 1; i < argc; i++) {
    char *path = argv[i];
    if (streq(path, "--pub")) {
      pub = 1;

      continue;
    }

		char* new_path = getpath(path, NULL);

		tinydir_file file;
    tinydir_file_open(&file, new_path);

    recurse_add_file(&state_, file);

    free(new_path);
  }

  // LINK STEP: LINK AND PUBLISH DEPS
  map_iterator file_iter = map_iterate(&state_.files);
  while (map_next(&file_iter)) {
    file *this = file_iter.x;

    // remove declared dependencies
    vector_iterator obj_iter = vector_iterate(&this->sorted_objects);
    while (vector_next(&obj_iter)) {
			object_t*obj = *(object_t**)obj_iter.x;

      // enable for.. debugging ;)
      // printf("%s: %s\n\n", *(char**)file_iter.key, obj->declaration);

      // drain dependencies
      vector_t old_deps = obj->deps;
      vector_iterator dep_iter = vector_iterate(&old_deps);

      obj->deps = vector_new(sizeof(char *));

      while (vector_next(&dep_iter)) {
        vector_t *dep_decls = map_find(&this->objects, dep_iter.x);

        // if an object in the same file exists and comes before this object,
        // then remove dependency
        if (dep_decls) {
          vector_iterator dep_decl_iter = vector_iterate(dep_decls);
          while (vector_next(&dep_decl_iter)) {
            if ((*(object_t**)dep_decl_iter.x)->object_id < obj->object_id) {
              continue;
            }
          }
        }

        vector_pushcpy(&obj->deps, dep_iter.x);  // otherwise keep dependency
      }

      vector_free(&old_deps);
    }

    // search for dependencies in file includes
    vector_iterator inc_iter = vector_iterate(&this->includes);

    while (vector_next(&inc_iter)) {
			include* inc = *(include**)inc_iter.x;
			if (inc->pub) continue;

      file* inc_file = map_find(&state_.files, &inc->path);

      if (!inc_file) {
        // if include does not reference a file that is searched, make public
        inc->pub = 1;
        continue;
      }

      vector_iterator obj_iter = vector_iterate(&this->sorted_objects);
      while (vector_next(&obj_iter)) {
				object_t*obj = *(object_t**)obj_iter.x;

        vector_iterator dep_iter = vector_iterate(&obj->deps);
        while (vector_next(&dep_iter)) {
          vector_t *inc_objs = map_find(&inc_file->objects, dep_iter.x);
          if (!inc_objs) continue;

          inc_file->pub = 1;

          vector_iterator iter_objs = vector_iterate(inc_objs);
          while (vector_next(&iter_objs)) {
            pub_deps(inc_file, *(object_t**)iter_objs.x);
          }

          // set obj to include inc_iter.i, in case obj ever gets published
          vector_pushcpy(&obj->include_deps, &inc);
        }
      }

      // private deps
      vector_iterator dep_iter = vector_iterate(&this->deps);
      while (vector_next(&dep_iter)) {
        vector_t *inc_objs = map_find(&inc_file->objects, dep_iter.x);
        if (!inc_objs) continue;

        inc_file->pub = 1;

        vector_iterator iter_objs = vector_iterate(inc_objs);
        while (vector_next(&iter_objs)) {
          pub_deps(inc_file, *(object_t**)iter_objs.x);
        }
      }
    }
  }

  // GENERATION: GENERATE ALL HEADERS / DELETE EMPTY HEADERS
  map_iterator filegen_iter = map_iterate(&state_.files);
  while (map_next(&filegen_iter)) {
    file *gen_this = filegen_iter.x;
    if (!gen_this->pub && !pub) continue;

    char *filename = *(char **)filegen_iter.key;
    // modify to header
    filename = heapcpy(strlen(filename) + 1, filename);
    filename[strlen(filename) - 1] = 'h';  //.c -> .h
    filename[strlen(filename)] = 0;

    FILE *handle = fopen(filename, "w");
    fprintf(handle, "// Automatically generated header.\n\n#pragma once\n");

    vector_t if_stack = vector_new(sizeof(if_stack));

		vector_iterator iter = vector_iterate(&gen_this->includes);
		while (vector_next(&iter)) {
			include* inc = *(include**)iter.x;
			if (!inc->pub) continue;

			set_ifs(gen_this, handle, &if_stack, &inc->ifs);

			fprintf(handle, "%s\n", inc->raw);
		}

    vector_iterator ordered_iter = vector_iterate(&gen_this->sorted_objects);
    while (vector_next(&ordered_iter)) {
			object_t*ordered_obj = *(object_t**)ordered_iter.x;

      if ((!pub && !ordered_obj->pub) || !ordered_obj->declaration) continue;

      vector_iterator inc_iter = vector_iterate(&ordered_obj->include_deps);
      while (vector_next(&inc_iter)) {
        // write include if doesnt exist
				include* inc = *(include**)inc_iter.x;

				if (!inc->pub) {
					inc->pub = 1;

					set_ifs(gen_this, handle, &if_stack, &inc->ifs);

          fprintf(handle, "%s\n", inc->raw);
        }
      }

			set_ifs(gen_this, handle, &if_stack, &ordered_obj->ifs);

      fprintf(handle, "%s\n", ordered_obj->declaration);
    }

    while (if_stack.length > 0) {
      vector_pop(&if_stack);
      fprintf(handle, "#endif\n");
    }

    fclose(handle);
  }
}
