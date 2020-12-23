//i just realized this is terrible ill probably rewrite it when i get bored enough

#include <stdio.h>
#include <stdlib.h>

#define MIN(a, b) (a < b ? a : b)

#include "../src/hashtable.h"
#include "../src/tinydir.h"
#include "../src/cfg.h"
#include "../src/util.h"
#include "../src/vector.h"

typedef struct {
  enum { if_none, if_def, if_ndef } kind;
  char* cond;
  char* acc; //accumulator stores the OR of all branches thus far for lazy elif handling
} if_t;

typedef struct {
  struct file *file;
  unsigned object_id;  // indexes into sorted_objects
  char* declaration;
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
	int in_def;
  int braces;
  int parens;

  char* path;
  char* filestart;
  char* file;
  vector_t if_stack;  // stack of #ifs to copy to objects
  vector_t if_braces; //# braces for each if

  file *current;

  map_t files;

  vector_t deps;
} state;

object_t* object_new(state *state_) {
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
}

typedef struct {
  char *start;
  int len;
} token;

// please dont read my shitty parser read the real one instead this one is
// really fucking bad

char* SKIP = "!*=<>/-+&[],.:|%^@;";
// braindump, just add more if it breaks
char *SKIP_WORDS[] = {"extern", "case",  "switch", "while", "for",
                      "do",     "const", "if",     "else",  "return"};
char *SKIP_TYPES[] = {"int", "char", "long", "double", "float", "void"};

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

int skip_word(state *state, char *word) {
	if (strncmp(word, state->file, strlen(word)) == 0) {
		char *start = state->file;
		state->file += strlen(word);

		if (ws(state->file) || !*state->file || strchr(SKIP, *state->file) ||
				strchr("{}", *state->file))
			return 1;
		else
			state->file = start;  // no skip or brace or ws after word, not parsed
	}

	return 0;
}

void parse_directive(state* state);

void parse_define(state *state) {
  while (*state->file != '\n' && *state->file != '\r' && *state->file && !parse_comment(state)) {
    if (skip_char(&state->file, '\\')) skip_char(&state->file, '\r');
    state->file++;
  }
}

int skip_list(state *state, char **skiplist, size_t size) {
  for (int i = 0; i < size / sizeof(char *); i++) {
    if (skip_word(state, skiplist[i])) return 1;
  }

  return 0;
}

int skip_nokeyword(state *state) {
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

  //braces/parens
  if (skip_char(&state->file, '{')) {
  	state->braces++; return 1;
  } else if (skip_char(&state->file, '}')) {
  	state->braces--; return 1;
  } else if (skip_char(&state->file, '(')) {
  	state->parens++; return 1;
  } else if (skip_char(&state->file, ')')) {
  	state->parens--; return 1;
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

int skip_ws_comment_once(state* state) {
	if (skip_char(&state->file, '#')) {
		parse_directive(state);
		return 1;
	} else if (ws(state->file)) {
		do state->file++; while (ws(state->file));
		return 1;
	} else if (skip_word(state, "__attribute__")) {
		char c;
		int p = state->parens;
		while ((c=*(state->file++))) {
			if (c=='(') state->parens++;
			else if (c==')') {
				state->parens--;
				if (state->parens==p) break;
			}
		}

		return 1;
	} else if (parse_comment(state)) {
		return 1;
	} else {
		return 0;
	}
}

int skip_ws_comment(state *state) {
	int skipped=0;
	while (1) {
		if (skip_ws_comment_once(state)) skipped=1;
		else break;
	}

	return skipped;
}

token parse_token(state *state) {
  if (!*state->file) return (token){.start=NULL, .len=0};

  token tok;
  tok.start = state->file;
  tok.len = 0;

  while (!ws(state->file) && *state->file && !strchr(SKIP, *state->file) &&
         !strchr("{}();", *state->file)) {
    tok.len++;
    state->file++;
  }

  return tok;
}

token parse_token_ws(state* state) {
	skip_ws_comment(state);
	return parse_token(state);
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
    else if (*fnptr == '\\' && *(fnptr+1) == '\\')
      slash = fnptr + 2;
  }

  if (!slash) return "";

  char *new_path = heap(slash - filename + 1);
  memcpy(new_path, filename, slash - filename);
  new_path[slash - filename] = 0;

  return new_path;
}

token parse_string_tok(state *state) {
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

char* get_cond(state *state) {
	skip_ws_comment(state);
	char* ifstart = state->file;

	parse_define(state);

	return range(ifstart, state->file);
}

include* get_inc(file* f, char* str, vector_t* ifs) {
	vector_iterator iter = vector_iterate(&f->includes);
	while (vector_next(&iter)) {
		include* inc = *(include**)iter.x;
		if (ifs->length == inc->ifs.length && streq(str, inc->path)) {
			vector_iterator if_iter = vector_iterate(ifs);
			int br=0;
			while (vector_next(&if_iter)) {
				if_t* other = vector_get(&inc->ifs, if_iter.i-1);
				if (!streq(((if_t*)if_iter.x)->cond, other->cond)) {
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

void skim(state* state, object_t* obj, char* breakchr) {
	object_t* new=NULL;
	char* prevstart=NULL;
	object_t* old=NULL;

	int is_static=0, is_inline=0;

	while (1) {
		int prevws=0;
		while (!strchr(breakchr, *state->file)) {
			if (skip_char(&state->file, ';')) {
				prevstart=NULL; //a subset of nokeywords should stop function parsing, but ex. pointers are allowed
				continue;
			}

			if (skip_ws_comment_once(state) || skip_nokeyword(state)) continue;
			else break;
		}

		if (strchr(breakchr, *state->file)) break;

		token tok = parse_token(state);
		char* str = token_str(state, tok);
		if (tok.len==0) break;

		int pub = !is_static || is_inline;

		if (!prevstart) prevstart = tok.start;

		int is_enum = token_eq(tok, "enum");
		if (token_eq(tok, "static")) {
			is_static=1; continue;
		} else if (token_eq(tok, "inline")) {
			is_inline=1; continue;
		} else if (token_eq(tok, "typedef")) {
			new = object_new(state);
			skip_ws_comment(state);

			token name = {.len=0};

			while (1) {
				skim(state, new, " (\t\n");

				skip_ws_comment(state);
				if (!skip_char(&state->file, '(')) break;
				else if (state->parens!=0) continue;

				state->parens++;
				skip_ws_comment(state);
				if (skip_char(&state->file, '*')) { //(*fnptr...
					name = parse_token_ws(state);
				}
			}

			if (!name.len) name = parse_token(state);
			skim(state, new, ";");
			skip_char(&state->file, ';');
			prevstart=NULL;

			str = token_str(state, name);
			object_ref(state, str, new);
			object_push(state, str, new);
			new->declaration = range(tok.start, state->file);
		} else if (is_enum || token_eq(tok, "struct") || token_eq(tok, "union")) {
			new = obj ? obj : object_new(state);
			token name = parse_token_ws(state);
			skip_ws_comment(state);

			if (name.len) str = heapstr("%s %s", str, token_str(state, name));

			int sep=1;
			if (skip_char(&state->file, '{') && !skip_char(&state->file, ';')) {
				if (is_enum) {
					while (!skip_char(&state->file, '}')) {
						if (skip_ws_comment(state) || skip_char(&state->file, ',')) continue;
						token enumeration = parse_token_ws(state);
						if (pub) object_ref(state, token_str(state, enumeration), new);
						skim(state, new, ",}");
					}
				} else {
					skim(state, new, "}");
					skip_char(&state->file, '}');
				}

				if ((sep=strchr(breakchr, *state->file)==NULL)) {
					skip_ws_comment(state);
					sep = skip_char(&state->file, ';');
				}

				if (pub) {
					if (!obj) object_push(state, str, new);
					if (name.len) object_ref(state, str, new);
				}
			}

			if (!obj) {
				if (!sep) {
					new->declaration = straffix(range(tok.start, state->file), ";");
				} else {
					new->declaration = range(tok.start, state->file);
					prevstart=NULL;
				}
			}
		} else if (prevstart && prevstart!=tok.start && !state->in_def && state->braces==0 && state->parens==0) {
			char* start = state->file;

			skip_ws_comment(state);
			int parsed=0;
			if (skip_char(&state->file, '=')) {
				new = old ? old : object_new(state);
				new->declaration = heapstr("extern %s;", range(prevstart, start));

				do skim(state, new, ";"); while (state->braces!=0 && state->parens!=0);
				skip_char(&state->file, ';');

				parsed=1;
			} else if (skip_char(&state->file, '(')) {
				new = old ? old : object_new(state);

				state->parens++;
				while (state->parens>0) {
					skip_char(&state->file, ',');
					skim(state, new, ",)");
					if (skip_char(&state->file, ')')) state->parens--;
				}

				if (!is_inline) new->declaration = straffix(range(prevstart, state->file), ";");

				skip_ws_comment(state);
				if (!skip_char(&state->file, ';') && skip_char(&state->file, '{') && state->braces==0) {
					state->braces++;
					while (state->braces>0) {
						skim(state, NULL, "}"); //dont add deps
						skip_char(&state->file, '}');
						state->braces--;
					}
				} else { //forward decl
					pub=0;
				}

				if (is_inline) new->declaration = range(prevstart, state->file);

				parsed=1;
			}

			if (parsed) {
				if (pub) {
					object_ref(state, str, new);
					if (!old) object_push(state, str, new);
				}

				is_static=0; is_inline=0;
				prevstart=NULL;
			}
		}

		if (new) {
			old = new;
			new = NULL;
		} else {
			vector_pushcpy(&state->current->deps, &str);
			if (obj) vector_pushcpy(&obj->deps, &str);
			old = NULL;
		}
	}
}

void parse_directive(state* state) {
	char* start = state->file-1; //before pound
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
					strpre(token_str(state, parse_string_tok(state)), dirname(state->path));

			path[strlen(path) - 1] = 'c';  //.h -> .c for finds and gen

			char* str = range(start, state->file);

			char* rpath = getpath(path);
			if (rpath) drop(path);

			include* inc = heapcpy(sizeof(include), &(include){.path=rpath ? rpath : path, .raw=str, .pub=rpath == NULL});
			vector_cpy(&state->if_stack, &inc->ifs);

			if (!get_inc(state->current, inc->path, &state->if_stack))
				vector_pushcpy(&state->current->includes, &inc);
		}
	} else if (skip_word(state, "if")) {
		char* cond = get_cond(state);
		vector_pushcpy(&state->if_stack, &(if_t){.kind = if_none, .cond=cond, .acc=heapstr("(%s)", cond)});
		vector_pushcpy(&state->if_braces, &state->braces);
	} else if (skip_word(state, "ifdef")) {
		vector_pushcpy(&state->if_stack, &(if_t){.kind = if_def, .cond = get_cond(state)});
		vector_pushcpy(&state->if_braces, &state->braces);
	} else if (skip_word(state, "ifndef")) {
		vector_pushcpy(&state->if_stack, &(if_t){.kind = if_ndef, .cond = get_cond(state)});
		vector_pushcpy(&state->if_braces, &state->braces);
	} else if (skip_word(state, "elif")) {
		if_t* cond = vector_get(&state->if_stack, state->if_stack.length - 1);
		if_t new_cond = {.kind=if_none};

		char* cond_str = get_cond(state);
		switch (cond->kind) {
			case if_none: {
				new_cond.cond = heapstr("(%s) && !(%s)", cond_str, cond->acc);
				new_cond.acc = heapstr("(%s) || %s", cond_str, cond->acc);
				drop(cond->acc);
				break;
			}
			case if_def: {
				new_cond.cond = heapstr("(%s) && !defined(%s)", cond_str, cond->cond);
				new_cond.acc = heapstr("(%s) || defined(%s)", cond_str, cond->cond);
				break;
			}
			case if_ndef: {
				new_cond.cond = heapstr("(%s) && defined(%s)", cond_str, cond->cond);
				new_cond.acc = heapstr("(%s) || !defined(%s)", cond_str, cond->cond);
				break;
			}
		}

		vector_pop(&state->if_stack);                 // pop old if
		vector_pushcpy(&state->if_stack, &new_cond);  // pop new else

		state->braces = *(int*)vector_get(&state->if_braces, state->if_stack.length-1);
	} else if (skip_word(state, "else")) {
		if_t* cond = vector_get(&state->if_stack, state->if_stack.length - 1);
		if_t new_cond;
		switch (cond->kind) {
			case if_none: {
				new_cond.kind = if_none;
				new_cond.cond = heapstr("!(%s)", cond->acc);
				new_cond.acc = cond->acc; //for drop() in endif

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
		state->braces = *(int*)vector_get(&state->if_braces, state->if_stack.length-1);
	} else if (skip_word(state, "endif")) {
		if_t* _if = vector_get(&state->if_stack, state->if_stack.length-1);
		if (_if->kind == if_none) drop(_if->acc);

		vector_pop(&state->if_stack);
		vector_pop(&state->if_braces);
	} else if (skip_word(state, "define")) {
		object_t* obj = object_new(state);
		char* name = token_str(state, parse_token_ws(state));

		state->in_def=1;

		do
			skim(state, obj, "\r\n");
		while (*((state->file++)-1)=='\\' || *(state->file-2)=='\r');
		state->file--;

		state->in_def=0;

		obj->declaration = range(start, state->file);  // no semicolon for defines
		object_ref(state, name, obj);
		object_push(state, name, obj);
	} else { // all other directives
		parse_define(state);
	}
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
  path = getpath(path);

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

  state->in_def = 0;
  state->braces = 0;
  state->parens = 0;

  state->if_stack = vector_new(sizeof(if_t));
  state->if_braces = vector_new(sizeof(int));

  state->current = new_file;

  skim(state, NULL, "");

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

  // PARSE: GO THROUGH ALL FILES AND PARSE OBJECTS N STUFF
  for (int i = 1; i < argc; i++) {
    char *path = argv[i];
    if (streq(path, "--pub")) {
      pub = 1;

      continue;
    }

		char* new_path = getpath(path);

		tinydir_file file;
    tinydir_file_open(&file, new_path);

    recurse_add_file(&state_, file);

    free(new_path);
  }

	vector_iterator dep_iter;
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
      dep_iter = vector_iterate(&old_deps);

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

      obj_iter = vector_iterate(&this->sorted_objects);
      while (vector_next(&obj_iter)) {
				object_t* obj = *(object_t**)obj_iter.x;

        dep_iter = vector_iterate(&obj->deps);
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
      dep_iter = vector_iterate(&this->deps);
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
		unsigned flen = strlen(filename);
		filename = heapcpy(flen + 1, filename);
    filename[flen-1] = 'h'; //.c -> .h
    filename[flen] = 0;

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
			object_t* ordered_obj = *(object_t**)ordered_iter.x;

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
