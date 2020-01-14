#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define MIN(a, b) (a < b ? a : b)

#include "tinydir.h"
#include "../src/util.h"
#include "../src/hashtable.h"
#include "../src/vector.h"

typedef struct {
	struct file* file;
	unsigned long object_id; //indexes into sorted_objects
	char* declaration;
	vector_t deps; //char*
	vector_t include_deps;

	int pub;
} object;

typedef struct file {
	map_t objects;
	vector_t deps; // file-wide deps from contents of functions/globals

	vector_t sorted_objects;
	vector_t includes; //resolved paths for lookup
	vector_t raw_includes; //string in #include "..."

	map_t pub_includes; //set of published raw includes

	int pub; //has any published objects
} file;

typedef struct {
	int braces;
	int parens;

	char* path;
	char* file;
	vector_t tokens;

	file* current;
	vector_t* deps;

	map_t files;
} state;

object* object_new(state* state_) {
	object* obj = heap(sizeof(object));
	obj->object_id = state_->current->sorted_objects.length;
	vector_pushcpy(&state_->current->sorted_objects, &obj);

	obj->file = state_->current;
	obj->deps = vector_new(sizeof(char*));
	obj->include_deps = vector_new(sizeof(char*));
	obj->pub = 0;

	state_->deps = &obj->deps;

	return obj;
}

typedef struct {
	char* start;
	int len;
} token;

//please dont read my shitty parser read the real one instead this one is really fucking bad

char* SKIP = "!*=<>/-+&[],.:|&%^@";
//braindump, just add more if it breaks
char* SKIP_WORDS[] = {"extern", "case", "switch", "while", "for", "do", "const", "if", "else", "return"};
char* SKIP_TYPES[] = {"int", "char", "long", "double", "float", "void"};

int ws(state* state) {
	return *state->file == ' ' || *state->file == '\r' || *state->file == '\n' || *state->file == '\t';
}

int parse_comment(state* state) {
	//skip comments
	if (strncmp(state->file, "//", 2)==0) {
		state->file+=2; //skip //
		while (*state->file && *state->file != '\n') state->file++;

		return 1;
	} else if (strncmp(state->file, "/*", 2)==0) {
		state->file+=2; //skip /*
		while (*state->file && strncmp(state->file, "*/", 2)!=0) state->file++;
		state->file+=2; //skip */

		return 1;
	} else return 0;
}

void parse_ws(state* state) {
	while (ws(state)) state->file++;
	if (parse_comment(state)) parse_ws(state);
}

void parse_define(state* state) {
	while (*state->file != '\n' && *state->file && !parse_comment(state)) {
		if (*state->file == '\\') state->file++;
		state->file++;
	}

	if (*state->file == '\n') state->file++;
}

int skip_word(state* state, char* word) {
	if (strncmp(word, state->file, strlen(word)) == 0) {
		char* start = state->file;
		state->file += strlen(word);

		if (ws(state) || !*state->file
				|| strchr(SKIP, *state->file)
				|| strchr("{}", *state->file))
			return 1;
		else state->file = start; //no skip or brace or ws after word, not parsed
	}

	return 0;
}

int skip_list(state* state, char** skiplist, size_t size) {
	for (int i = 0; i < size / sizeof(char*); i++) {
		if (skip_word(state, skiplist[i])) return 1;
	}

	return 0;
}

void parse_skip(state* state) {
	parse_ws(state);

	//skip strings
	if (*state->file == '\"') {
		state->file++; //skip "
		while (*state->file != '\"' && *state->file) {
			if (*state->file == '\\') state->file++;
			state->file++;
		}

		state->file++; //skip "

		return parse_skip(state);
	}

	if (*state->file == '\'') {
		state->file++; //skip '
		if (*state->file == '\\') state->file++;
		state->file++; //skip char
		state->file++; //skip '

		return parse_skip(state);
	}

	//skip reserved tokens or numbers
	if (*state->file && (strchr("0123456789", *state->file) || strchr(SKIP, *state->file))) {
		state->file++;
		return parse_skip(state);
	}

	if (skip_list(state, SKIP_WORDS, sizeof(SKIP_WORDS))) {
		return parse_skip(state);
	}
}

int skip_sep(state* state) {
	if (*state->file == ';') {
		state->file++;
		return 1;
	} else return 0;
}

int parse_start_paren(state* state) {
	parse_skip(state);
	if (!*state->file) return 0;

	if (*state->file == '(' && state->parens == 0) {
		state->file++;
		return 1;
	} else return 0;
}

int parse_end_paren(state* state) {
	parse_skip(state);
	if (!*state->file) return 1;
	if (skip_sep(state)) return parse_end_paren(state);

	if (*state->file == ')') {
		state->file++;
		if (state->parens == 0) {
			return 1;
		} else {
			state->braces--;
			return parse_end_paren(state);
		}
	} else return 0;
}

int skip_paren(state* state) {
	if (*state->file == '(') {
		state->parens++;
		state->file++;
		return 1;
	} else if (*state->file == ')') {
		state->parens--;
		state->file++;
		return 1;
	} else return 0;
}

int parse_peek_brace(state* state) {
	parse_skip(state);
	if (!*state->file) return 0;

	if (*state->file == '{' && state->braces == 0) return 1;
	else return 0;
}

int parse_start_brace(state* state) {
	parse_skip(state);

	if (*state->file == '{' && state->braces == 0) {
		state->file++;
		return 1;
	} else return 0;
}

int parse_end_brace(state* state) {
	parse_skip(state);

	if (skip_paren(state) || skip_sep(state))
		return parse_end_brace(state);

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
	} else return 0;
}

int skip_braces(state* state) {
	if (*state->file == '{') {
		state->braces++;
		state->file++;
		return 1;
	} else if (*state->file == '}') {
		state->braces--;
		state->file++;
		return 1;
	} else return 0;
}

int parse_sep(state* state) {
	parse_skip(state);
	if (skip_braces(state) || skip_paren(state)) return parse_sep(state);

	return *state->file == ';';
}

/// does a crappy check to see if previous token was probably a name
int parse_name(state* state) {
	parse_ws(state);
	if (!*state->file) return 0;

	if (strchr("[(=,", *state->file)) return 1;
	else return 0;
}

token* parse_token_noskip(state* state) {
	if (!*state->file) return NULL;

	token tok;
	tok.start = state->file;
	tok.len = 0;

	while (!ws(state) && *state->file
			&& !strchr(SKIP, *state->file)
			&& !strchr("{}();", *state->file)) {

		tok.len++;
		state->file++;
	}

	return vector_pushcpy(&state->tokens, &tok);
}

/// parses identifiers
token* parse_token(state* state) {
	parse_skip(state);

	return parse_token_noskip(state);
}

token* parse_token_braced(state* state) {
	parse_skip(state);
	if (skip_sep(state) || skip_paren(state) || skip_braces(state)) {
		return parse_token_braced(state);
	}

	return parse_token_noskip(state);
}

int token_eq(token* tok, char* str) {
	return tok->len == strlen(str) && strncmp(tok->start, str, tok->len) == 0;
}

char* range(char* start, char* end) {
	char* str = heap(end - start + 1);
	memcpy(str, start, end - start);
	str[end - start] = 0;
	return str;
}

char* token_str(state* state, token* tok) {
	if (tok == NULL) {
		fprintf(stderr, "expected token at %s...",
						range(state->file, state->file + MIN(10, strlen(state->file))));

		exit(1);
	}

	char* str = heap(tok->len + 1);
	memcpy(str, tok->start, tok->len);
	str[tok->len] = 0;
	return str;
}

char* prefix(char* str, char* prefix) {
	if (*prefix == 0) return str;

	char* new_str = heap(strlen(str) + strlen(prefix) + 1);
	memcpy(new_str, prefix, strlen(prefix));
	memcpy(new_str + strlen(prefix), str, strlen(str) + 1); //copy null byte
	free(str);

	return new_str;
}

char* affix(char* str, char* affix) {
	if (*affix == 0) return str;

	char* new_str = heap(strlen(str) + strlen(affix) + 1);
	memcpy(new_str, str, strlen(str) + 1);
	memcpy(new_str + strlen(str), affix, strlen(affix) + 1);
	free(str);

	return new_str;
}

char* dirname(char* filename) {
	char* slash = NULL;

	for (char* fnptr = filename; *fnptr; fnptr++) {
		if (*fnptr == '/')
			slash = fnptr + 1; //include slash
		else if (*fnptr == '\\' && *fnptr + 1 == '\\')
			slash = fnptr + 2;
	}

	if (!slash) return "";

	char* new_path = heap(slash - filename + 1);
	memcpy(new_path, filename, slash - filename);
	new_path[slash - filename] = 0;

	return new_path;
}

//hello sorry but this is stolen because i am relaly tired but its stolenfrom jonathan leffer on SO
//what a nice dude appparentlly this is 31 years old and it  probably some kind of shit
//but im not reading it

void clnpath(char *path)
{
	char           *src;
	char           *dst;
	char            c;
	int             slash = 0;

	/* Convert multiple adjacent slashes to single slash */
	src = dst = path;
	while ((c = *dst++ = *src++) != '\0')
	{
		if (c == '/')
		{
			slash = 1;
			while (*src == '/')
				src++;
		}
	}

	if (slash == 0)
		return;

	/* Remove "./" from "./xxx" but leave "./" alone. */
	/* Remove "/." from "xxx/." but reduce "/." to "/". */
	/* Reduce "xxx/./yyy" to "xxx/yyy" */
	src = dst = (*path == '/') ? path + 1 : path;
	while (src[0] == '.' && src[1] == '/' && src[2] != '\0')
		src += 2;
	while ((c = *dst++ = *src++) != '\0')
	{
		if (c == '/' && src[0] == '.' && (src[1] == '\0' || src[1] == '/'))
		{
			src++;
			dst--;
		}
	}
	if (path[0] == '/' && path[1] == '.' &&
			(path[2] == '\0' || (path[2] == '/' && path[3] == '\0')))
		path[1] = '\0';

	/* Remove trailing slash, if any.  There is at most one! */
	/* dst is pointing one beyond terminating null */
	if ((dst -= 2) > path && *dst == '/')
		*dst++ = '\0';
}

token* parse_string(state* state) {
	parse_ws(state);

	if (*state->file == '\"') {
		token* tok = vector_push(&state->tokens);

		state->file++;
		tok->start = state->file;
		tok->len = 0;

		while (*state->file != '\"' && *state->file) {
			if (*state->file == '\\') {
				state->file++;
				tok->len++;
			}

			state->file++;
			tok->len++;
		}

		state->file++;
		return tok;
	} else {
		return NULL;
	}
}

void add_dep(state* state, char* dep) {
	vector_pushcpy(state->deps, &dep);
}

object* parse_object(state* state, object* super, int static_);

int skip_type(state* state) {
	parse_skip(state);

	if (skip_sep(state) || skip_paren(state) || skip_braces(state)) {
		return skip_type(state);
	}

	if (skip_word(state, "unsigned")) {
		parse_ws(state);
		skip_list(state, SKIP_TYPES, sizeof(SKIP_TYPES));
		return 1;
	} else if (skip_list(state, SKIP_TYPES, sizeof(SKIP_TYPES))) {
		return 1;
	} else {
		return 0;
	}
}

object* parse_object(state* state, object* super, int static_) {
	if (super && skip_type(state)) {
		return NULL;
	}

	object* obj = NULL;
	char* name = NULL;

	vector_t* old_deps = state->deps;

	token* first = parse_token_braced(state);

	if (!first) return NULL;
	char* start = first->start;

	//modified and unmodified type-skipper
	if (token_eq(first, "#include") && !super) {
		parse_ws(state);
		if (*state->file == '<') {
			parse_define(state);
			char* str = range(start, state->file);
			map_insert(&state->current->pub_includes, &str);
			return NULL;
		}

		char* path = prefix(token_str(state, parse_string(state)), dirname(state->path));
		
		FILE* file = fopen(path, "a");
		fclose(file);
		
		path = realpath(path, NULL);

		vector_pushcpy(&state->current->includes, &path);

		char* str = range(start, state->file);
		vector_pushcpy(&state->current->raw_includes, &str);
		return NULL;
	} else if (token_eq(first, "#define") && !super) {
		obj = object_new(state);
		name = token_str(state, parse_token(state));

		parse_define(state);

		obj->declaration = range(start, state->file); //no semicolon for defines
	} else if (!super && *start == '#') { //all other directives
		parse_define(state);
		return NULL;

	} else if (token_eq(first, "typedef")) {		
		obj = parse_object(state, NULL, static_); //parse thing inside

		name = token_str(state, parse_token(state));

		if (!obj) {
			//probably a fwd declaration / raw dep, make empty obj
			obj = object_new(state);
		}

		obj->declaration = affix(range(start, state->file), ";");
		skip_sep(state);
	} else if (token_eq(first, "enum")) {
		obj = super ? super : object_new(state); //choose object to reference depending on superior object

		name = NULL; //anonymous default
		if (!parse_peek_brace(state))
			name = prefix(token_str(state, parse_token(state)), "enum ");

		if (parse_start_brace(state)) {
			while (!parse_end_brace(state)) {
				char* str = token_str(state, parse_token(state));
				map_insertcpy(&state->current->objects, &str, &obj);
			}
		} else if (name) {
			add_dep(state, name);
			return NULL;
		}

		//skip decl if superior object exists
		if (!super) obj->declaration = affix(range(start, state->file), ";");

	} else if (token_eq(first, "struct") || token_eq(first, "union")) {
		char* kind_prefix;
		if (token_eq(first, "struct")) kind_prefix = "struct ";
		else if (token_eq(first, "union")) kind_prefix = "union ";
		else return NULL;

		if (!parse_peek_brace(state)) {
			name = prefix(token_str(state, parse_token(state)), kind_prefix);
		}

		if (parse_start_brace(state)) {
			obj = super ? super : object_new(state); //used so lower-order objects reference most superior object

			while (!parse_end_brace(state)) {
				parse_object(state, obj, static_); //parse field type, add deps
				if (!parse_sep(state)) parse_token(state); // parse field name or dont (ex. anonymous union)
				//parse any addendums
				if (!parse_sep(state)) {
					while (!parse_sep(state)) {
						parse_object(state, 0, 1);
					}
				}

				parse_ws(state);
			}

			if (!super) obj->declaration = affix(range(start, state->file), ";");
		} else if (name) {
			add_dep(state, name);
			return NULL;
		}
	} else {
		char* str = token_str(state, first);
		add_dep(state, str);
		return NULL;
	}

	state->deps = old_deps;

	//superior object means that there is already an object that encapsulates this one
	if (obj && name && !super && !static_) {
		map_insertcpy(&state->current->objects, &name, &obj);
	}

	if (obj) {
		return obj;
	}

	return NULL;
}

object* parse_global_object(state* state) {
	vector_t* old_deps = state->deps;
	object* obj = object_new(state);

	int static_ = 0;

	parse_skip(state);
	if (skip_word(state, "static")) {
		static_ = 1;
	}

	char* start = state->file;

	object* ty = parse_object(state, NULL, static_);
	char* reset = state->file; //reset to ty if not a global variable/function

	token* name_tok = parse_token(state);
	if (!name_tok) return ty;

	char* name = token_str(state, name_tok);

	char* after_name = state->file;
	parse_ws(state);

	if (*state->file == '=') { //global
		obj->declaration = heapstr("extern %s;", range(start, after_name));

		while (!parse_sep(state)) {
			while (skip_type(state));
			char* str = token_str(state, parse_token_braced(state));
			vector_pushcpy(&state->current->deps, &str);
		}

		skip_sep(state);

	} else if (parse_start_paren(state)) { //function
		while (!parse_end_paren(state)) {
			parse_object(state, obj, static_);
			if (!parse_name(state)) parse_token(state); //parse argument name
		}

		obj->declaration = affix(range(start, state->file), ";");

		if (parse_start_brace(state)) {
			while (!parse_end_brace(state)) {
				while (skip_type(state));

				token* tok = parse_token_braced(state);
				char* str = token_str(state, tok);
				vector_pushcpy(&state->current->deps, &str);
			}
		}
	} else {
		state->file = reset;
		return ty;
	}

	state->deps = old_deps;
	if (!static_) map_insertcpy(&state->current->objects, &name, &obj);
	return obj;
}

char* ext(char* filename) {
	char* dot = "";
	for (; *filename; filename++) {
		if (*filename == '.') dot = filename;
	}

	return dot;
}

void pub_deps(file* file, object* obj) {
	if (obj->pub) return; //already marked
	obj->pub = 1;

	vector_iterator dep_iter = vector_iterate(&obj->deps);
	while (vector_next(&dep_iter)) {
		object** obj_dep = map_find(&file->objects, dep_iter.x);
		if (!obj_dep) continue;

		pub_deps(file, *obj_dep);
	}
}

void add_file(state* state, char* path, char* filename) {
	if (strcmp(ext(filename), ".c") != 0) return;
	path = realpath(path, NULL);

	if (!path) {
		fprintf(stderr, "%s does not exist", filename);
		return;
	}

	FILE* handle = fopen(path, "rb");
	if (!handle) {
		fprintf(stderr, "cannot read %s", filename);
		free(path);
		return;
	}

	//pretty much copied from frontend
	fseek(handle, 0, SEEK_END);
	unsigned long len = ftell(handle);

	rewind(handle);

	char* str = heap(len + 1);

	fread(str, len, 1, handle);
	fclose(handle);

	str[len] = 0;

	path = heapcpy(strlen(path)+1, path); //copy path for modifications
	path[strlen(path) - 1] = 'h'; //.c -> .h for finds and gen

	fclose(handle);

	file* new_file = map_insert(&state->files, &path).val;

	new_file->objects = map_new();
	map_configure_string_key(&new_file->objects, sizeof(object*));
	new_file->sorted_objects = vector_new(sizeof(object*));
	
	new_file->deps = vector_new(sizeof(char*));

	new_file->pub = 0;
	new_file->includes = vector_new(sizeof(char*));
	new_file->raw_includes = vector_new(sizeof(char*));

	new_file->pub_includes = map_new();
	map_configure_string_key(&new_file->pub_includes, 0);

	state->file = str;
	state->path = path;

	state->braces = 0;
	state->parens = 0;

	state->tokens = vector_new(sizeof(token));

	state->current = new_file;
	state->deps = NULL;

	while (*state->file) {
		parse_global_object(state);
	}

	vector_clear(&state->tokens);

	free(str);
}

void recurse_add_file(state* state, tinydir_file file) {
	if (file.is_dir) {
		tinydir_dir dir;
		tinydir_open(&dir, file.path);
		tinydir_next(&dir); //skip .
		tinydir_next(&dir); //skip ..

		while (dir.has_next) {
			tinydir_readfile(&dir, &file);
			recurse_add_file(state, file);

			tinydir_next(&dir);
		}
		
		tinydir_close(&dir);
	} else {
		add_file(state, file.path, file.name);
	}
}

int main(int argc, char** argv) {
	state state_ = {.files=map_new(), .deps=NULL};
	map_configure_string_key(&state_.files, sizeof(file));

	int pub = 0; //--pub makes everything public

	// PARSE STEP: GO THROUGH ALL FILES AND PARSE OBJECTS N STUFF
	for (int i = 1; i < argc; i++) {
		char* path = argv[i];
		if (strcmp(path, "--pub")) {
			pub = 1;
		}

		tinydir_file file;
		tinydir_file_open(&file, path);

		recurse_add_file(&state_, file);
	}

	//LINK STEP: PUBLISH DEPS
	map_iterator file_iter = map_iterate(&state_.files);
	while (map_next(&file_iter)) {
		file* this = file_iter.x;
		
		//search for dependencies in file includes
		vector_iterator inc_iter = vector_iterate(&this->includes);

		while (vector_next(&inc_iter)) {
			file* inc_file = map_find(&state_.files, inc_iter.x);
			if (!inc_file) continue;

			vector_iterator obj_iter = vector_iterate(&this->sorted_objects);
			while (vector_next(&obj_iter)) {
				object* obj = *(object**)obj_iter.x;

				vector_iterator dep_iter = vector_iterate(&obj->deps);
				while (vector_next(&dep_iter)) {
					object** inc_obj = map_find(&inc_file->objects, dep_iter.x);
					if (!inc_obj) continue;

					inc_file->pub = 1;
					pub_deps(inc_file, *inc_obj);
					//set obj to include inc_iter.i, in case obj ever gets published
					vector_pushcpy(&obj->include_deps, vector_get(&this->raw_includes, inc_iter.i-1));
				}
			}

			vector_iterator dep_iter = vector_iterate(&this->deps);
			while (vector_next(&dep_iter)) {
				object** inc_obj = map_find(&inc_file->objects, dep_iter.x);
				if (!inc_obj) continue;

				inc_file->pub = 1;
				pub_deps(inc_file, *inc_obj);
			}
		}
	}

	//GENERATION: GENERATE ALL HEADERS / DELETE EMPTY HEADERS
	map_iterator filegen_iter = map_iterate(&state_.files);
	while (map_next(&filegen_iter)) {
		file* gen_this = filegen_iter.x;
		if (!gen_this->pub) continue;

		char* filename = *(char**) filegen_iter.key;

		FILE* handle = fopen(filename, "w");
		fprintf(handle, "// Automatically generated header.\n\n#pragma once\n");

		map_iterator pub_inc_iter = map_iterate(&gen_this->pub_includes);
		while (map_next(&pub_inc_iter)) {
			char* inc = *(char**) pub_inc_iter.key;
			fwrite(inc, strlen(inc), 1, handle);

			fprintf(handle, "\n");
		}

		vector_iterator ordered_iter = vector_iterate(&gen_this->sorted_objects);
		while (vector_next(&ordered_iter)) {
			object* ordered_obj = *(object**) ordered_iter.x;
			if (!pub && !ordered_obj->pub) continue;
			
			vector_iterator inc_iter = vector_iterate(&ordered_obj->include_deps);
			while (vector_next(&inc_iter)) {
				//write include if doesnt exist
				if (!map_insert(&gen_this->pub_includes, inc_iter.x).exists) {
					char* inc = *(char**) inc_iter.x;
					fwrite(inc, strlen(inc), 1, handle);
					
					fprintf(handle, "\n");
				}
			}

			fwrite(ordered_obj->declaration, strlen(ordered_obj->declaration), 1, handle);

			fprintf(handle, "\n");
		}

		fclose(handle);
	}
}
