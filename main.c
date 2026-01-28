#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

// UTIL

#define nukeif(x) if (__builtin_expect(x, 0)) __builtin_trap()

#define CHUNK16 256
#define CHUNK24 256
#define CHUNK32 128
#define CHUNK64  64
#define CHUNK128 32

#define new_pool(x)\
	typedef struct p##x {char p[x];} p##x;				    \
	p##x pool##x [CHUNK##x ];					    \
	unsigned short table##x[CHUNK##x ];				    \
	unsigned short lookup##x;					    \
	__attribute__((constructor, cold)) static void init_pool##x (void) {\
		for (unsigned short i = 0; i < CHUNK##x; i++)		    \
			table##x [i] = i+1;				    \
		lookup##x = 0;						    \
	}								    \
	static void* borrow_pool##x (void) {				    \
		if (lookup##x != CHUNK##x ) {				    \
			void* res = pool##x [lookup##x ].p;		    \
			lookup##x = table##x [lookup##x ];		    \
			return res;					    \
		}							    \
		return NULL;						    \
	}								    \
	static bool return_pool##x (void* p) {				    \
		int i = ((intptr_t)p - (intptr_t)pool##x ) / sizeof(p##x ); \
		if (i < 0 || i >= CHUNK##x ) return false;		    \
		table##x [i] = lookup##x ;				    \
		lookup##x = i;						    \
		return true;						    \
	} struct _

new_pool(16);  // vars, small char arrays
new_pool(24);  // array, string objects
new_pool(32);  // char arrays
new_pool(64);  // char arrays
new_pool(128); // var, char arrays

static void checked_free(void* p) {
	nukeif(!p);
	if (return_pool16(p)) return;
	if (return_pool24(p)) return;
	if (return_pool32(p)) return;
	if (return_pool64(p)) return;
	if (return_pool128(p)) return;
	free(p);
}

static void* checked_malloc(size_t n) {
	nukeif(!n);
	
#define try_borrow(x, y)		\
	if (n > x && n <= y) {		\
		res = borrow_pool##y ();\
		if (res) return res;	\
	} struct _
	
	void* res = NULL;
	try_borrow(0, 16);
	try_borrow(16, 24);
	try_borrow(24, 32);
	try_borrow(32, 64);
	try_borrow(64, 128);
	res = malloc(n);
	nukeif(!res);
	return res;
}

static void* checked_realloc(void* p, size_t n) {
	if (!n) {
		checked_free(p);
		return NULL;
	}
	void* res = NULL;
	
#define try_move(y, x)						      \
	int i##x = ((intptr_t)p - (intptr_t)pool##x ) / sizeof(p##x );\
	if (i##x > -1 && i##x < CHUNK##x ) {			      \
		if (n > y && n <= x) return p;			      \
		res = checked_malloc(n);			      \
		memcpy(res, p, (x<n)?x:n);			      \
		return_pool##x (p);				      \
		return res;					      \
	} struct _
	
	try_move(0, 16);
	try_move(16, 24);
	try_move(24, 32);
	try_move(32, 64);
	try_move(64, 128);
	res = realloc(p, n);
	nukeif(!res);
	return res;
}

static size_t checked_strlen(const char* s) {
	nukeif(!s);
	size_t len = 0;
	while (*(s++)) len++;
	return len;
}

static double s_tod(const char* s) {
	nukeif(!s);
	errno = 0;
	if (*s == '\0') {
		errno = EINVAL;
		return 0.0;
	}
	char* end;
	double res = strtod(s, &end);
	if (end == s) {
		errno = EINVAL;
		return 0.0;
	}
	while ((*end == ' ' || *end == '\t') && *end != '\0') end++;
	if (*end == '\0') return res;
	errno = EINVAL;
	return 0.0;
}

static long long int s_toi(const char* s) {
	nukeif(!s);
	errno = 0;
	if (*s == '\0') {
		errno = EINVAL;
		return 0;
	}
	char* end;
	long long int res = strtoll(s, &end, 0);
	if (end == s) {
		errno = EINVAL;
		return 0;
	}
	while ((*end == ' ' || *end == '\t') && *end != '\0') end++;
	if (*end == '\0') return res;
	errno = EINVAL;
	return 0;
}

static size_t s_tou(const char* s) {
	nukeif(!s);
	errno = 0;
	if (*s == '\0') {
		errno = EINVAL;
		return 0;
	}
	char* end;
	long long int res = strtoull(s, &end, 0);
	if (end == s) {
		errno = EINVAL;
		return 0;
	}
	while ((*end == ' ' || *end == '\t') && *end != '\0') end++;
	if (*end == '\0') return res;
	errno = EINVAL;
	return 0;
}

// DATA

bool is_file = false;

#define try(x)                     \
	RES = x;                   \
	if (RES) return RES

#define tryor(x, y)                \
	do {                       \
		RES = x;           \
		if (RES) {         \
			y;         \
			return RES;\
		}                  \
	} while (0)

#define ok return RESULT_OK

typedef enum {
	RESULT_OK = 0,
	RESULT_RETURN = 1,
	RESULT_ERROR = 2
} result;

result RES = RESULT_OK;

#define open '{'
#define next ';'
#define close '}'

#define expropen '('
#define exprclose ')'

typedef struct __attribute__((packed)) {
	char* p;
	size_t len;
	size_t cap;
} string;

typedef struct {
	char* p;
	size_t len;
} constr;

static int constrcmp(constr a, constr b) {
	nukeif(!a.p);
	nukeif(!b.p);
	if (a.len != b.len) return (int)a.len - b.len;
	if (a.len == 0) return 0;
	while(a.len && (*a.p == *b.p)) {
		a.len--;
		a.p++;
		b.p++;
	}
	if (a.len == 0) return 0;
	return (int)((unsigned char)*a.p) - (unsigned char)*b.p;
}

constr constr_from(string s) {
	return (constr) {
		.p = s.p,
		.len = s.len
	};
}

typedef struct __attribute__((packed)) arr {
	struct var* p;
	size_t len;
	size_t cap;
} arr;

typedef struct var {
	union {
		void* _;
		string* s;
		double n;
		long long int i;
		size_t u;
		bool b;
		char* f;
		char* x;
		char* m;
		arr* a;
		FILE* ft;
		FILE* fb;
		string* e;
	} v;
	enum : size_t {
		TYPE_NONE,
		TYPE_STRING,
		TYPE_NUMBER,
		TYPE_INTEGER,
		TYPE_UINTEGER,
		TYPE_BOOLEAN,
		TYPE_FUNCTION,
		TYPE_EXPRESSION,
		TYPE_MACRO,
		TYPE_ARRAY,
		TYPE_FILE_TXT,
		TYPE_FILE_BIN,
		TYPE_ERROR
	} t;	
} var;

typedef struct frame {
	struct frame* n;
	var v;
} frame;

static string string_from(const char* s);
static string string_copy(const string* from);
static void var_clear(var* v);
static arr arr_copy(const arr l);
static void string_terminate(string* to);

static var var_copy(const var v) {
	var res;
	res.t = v.t;
	switch(v.t) {
		case TYPE_NONE:
		case TYPE_NUMBER:
		case TYPE_INTEGER:
		case TYPE_UINTEGER:
		case TYPE_BOOLEAN:
		case TYPE_FUNCTION:
		case TYPE_EXPRESSION:
		case TYPE_MACRO:
			res = v;
			break;
		case TYPE_STRING:
			res.v.s = checked_malloc(sizeof(string));
			*res.v.s = string_copy(v.v.s);
			break;
		case TYPE_ERROR:
			res.v.e = checked_malloc(sizeof(string));
			*res.v.e = string_copy(v.v.e);
			break;
		case TYPE_ARRAY:
			res.v.a = checked_malloc(sizeof(arr));
			*res.v.a = arr_copy(*v.v.a);
			break;
		default:
			__builtin_unreachable();
	}
	return res;
}

static arr arr_copy(const arr l) {
	arr res;
	res = l;
	res.p = checked_malloc(sizeof(var)*res.cap);
	for (size_t i = 0; i < res.len; i++) {
		res.p[i] = var_copy(l.p[i]);
	}
	return res;
}

static void arr_clear(arr* l) {
	nukeif(!l);
	for (size_t i = 0; i < l->len; i++) {
		var_clear(&l->p[i]);
	}
	l->len = 0;
	l->cap = 0;
	if (l->p) checked_free(l->p);
}

static void arr_append(arr* l, var v) {
	nukeif(!l);
	l->len++;
	if (l->len > l->cap) {
		if (l->cap == 0) l->cap = 8;
		else l->cap *= 2;
		l->p = checked_realloc(l->p, sizeof(var)*l->cap);
	}
	l->p[l->len-1] = v;
}

static void arr_insert(arr* l, size_t n, var v) {
	nukeif(!l);
	if (n > l->len) {
		var_clear(&v);
		return;
	}
	l->len++;
	if (l->len > l->cap) {
		if (l->cap == 0) l->cap = 8;
		else l->cap *= 2;
		l->p = checked_realloc(l->p, sizeof(var)*l->cap);
	}
	for (size_t i = l->len-1; i > n; i--) {
		l->p[i] = l->p[i-1];
	}
	l->p[n] = v;
}

static void arr_remove(arr* l, size_t i) {
	nukeif(!l);
	if (i >= l->len) return;
	l->len--;
	var_clear(&l->p[i]);
	for (size_t x = i; x < l->len; x++) {
		l->p[x] = l->p[x+1];
	}
	if (l->len) {
		if (l->len < l->cap/2 && l->len > 4) {
			l->cap /= 2;
			l->p = checked_realloc(l->p, sizeof(var)*l->cap);
		}
	} else {
		l->cap = 0;
		checked_free(l->p);
		l->p = NULL;
	}
}

static var arr_pop(arr* l, size_t i) {
	nukeif(!l);
	var res;
	if (i >= l->len) {
		res.t = TYPE_NONE;
		res.v._ = NULL;
		return res;
	}
	l->len--;
	res = l->p[i];
	for (size_t x = i; x < l->len; x++) {
		l->p[x] = l->p[x+1];
	}
	if (l->len) {
		if (l->len < l->cap/2 && l->len > 4) {
			l->cap /= 2;
			l->p = checked_realloc(l->p, sizeof(var)*l->cap);
		}
	} else {
		checked_free(l->p);
		l->p = NULL;
	}
	return res;
}

static string string_from(const char* s) {
	nukeif(!s);
	string res;
	res.len = checked_strlen(s);
	res.cap = 16;
	while (res.cap <= res.len) res.cap*=2;
	res.p = checked_malloc(res.cap);
	strcpy(res.p, s);
	return res;
}

static string string_copy(const string* from) {
	nukeif(!from->p);
	string res;
	res.len = from->len;
	res.cap = from->cap;
	res.p = checked_malloc(res.cap);
	memcpy(res.p, from->p, res.len);
	return res;
}

__attribute__((hot))
static void string_pushc(string* to, const char c) {
	nukeif(!to);
	if (to->len == to->cap) {
		to->cap *= 2;
		to->p = checked_realloc(to->p, to->cap);
	}
	to->p[to->len++] = c;
}

static void string_terminate(string* to) {
	nukeif(!to);
	if (to->len == to->cap) {
		to->cap *= 2;
		to->p = checked_realloc(to->p, to->cap);
	}
	to->p[to->len] = '\0';
}

static void string_pushs(string* to, const char* s) {
	nukeif(!to);
	nukeif(!s);
	size_t slen = checked_strlen(s);
	while (to->len + slen > to->cap)
		to->cap *= 2;
	to->p = checked_realloc(to->p, to->cap);
	memcpy(to->p + to->len, s, slen);
	to->len += slen;
}

static void string_cat(string* restrict to, const string* from) {
	nukeif(!to);
	nukeif(!from);
	if (to->len + from->len > to->cap) {
		to->cap = to->len + from->len;
		to->p = checked_realloc(to->p, to->cap);
	}
	memcpy(to->p + to->len, from->p, from->len);
	to->len += from->len;
}

static void f_push(void);
static void f_free(void);
static void f_collapse(void);
static void f_pushc(char c);
static void f_pushs(const char* s);
static void f_replaces(const char* s);
static void f_replaceu(size_t n);
static char* f_refs(void);
static void f_terminate(void);

struct token {
	constr k;
	union {
		result (*fp)(void);
		var v;
		char* f;
		char* e;
		char* m;
		char* c;
	} p;
	enum {
		TOKEN_FUNCP,
		TOKEN_VAR,
		TOKEN_FUNC,
		TOKEN_EXPR,
		TOKEN_MACRO,
		TOKEN_CODE
	} t;
};

struct meth {
	constr k;
	union {
		result (*meth)(var*);
		char* f;
		char* e;
		char* m;
	} p;
	enum {
		METHOD_FUNCP,
		METHOD_FUNC,
		METHOD_EXPR,
		METHOD_MACRO
	} t;
};

int meth_cmp(void* restrict l, void* r) {
	nukeif(!l);
	nukeif(!r);
	return constrcmp((*(struct meth*)l).k, (*(struct meth*)r).k);
}

void meth_kill(void* x) {
	nukeif(!x);
	struct meth* m = x;
	if (m->t != METHOD_FUNCP) {
		checked_free(m->k.p);
		checked_free(m);
	}
}

typedef struct __attribute__((packed)) {
	size_t len;
	size_t cap;
	void** v;
	void (*kill)(void*);
	int (*cmp)(void*, void*);
} flatmap;

typedef struct {
	size_t len;
	size_t cap;
	flatmap* p;
	void (*kill)(void*);
	int (*cmp)(void*, void*);
} flatmaps;

static flatmap flatmap_init(void(*kill)(void*), int(*cmp)(void*, void*), size_t cap) {
	flatmap res;
	res.len = 0;
	res.cap = cap;
	if (cap != 0) res.v = checked_malloc(sizeof(void*)*res.cap);
	else res.v = NULL;
	res.kill = kill;
	res.cmp = cmp;
	return res;
}

static void flatmap_insert(flatmap* x, void* y) {
	nukeif(!x);
	nukeif(!y);
	if (x->len == 0) {
		if (x->cap == 0) {
			x->cap = 8;
			x->v = checked_malloc(sizeof(void*)*x->cap);
		}
		x->len++;
		x->v[0] = y;
		return;
	}
	long long int start = 0;
	long long int end = x->len - 1;
	long long int mid = 0;
	do {
		mid = (start + end)/2;
		if (x->cmp(x->v[mid], y) < 0) {
			start = mid + 1;
			mid++;
			continue;
		}
		if (x->cmp(x->v[mid], y) == 0) {
			x->kill(x->v[mid]);
			x->v[mid] = y;
			return;
		}
		if (x->cmp(x->v[mid], y) > 0) {
			end = mid - 1;
			continue;
		}
	} while (start <= end);
	x->len++;
	if (x->len > x->cap) {
		x->cap *= 2;
		x->v = checked_realloc(x->v, sizeof(void*)*x->cap);
	}
	for (size_t i = x->len-1; i > mid; i--) {
		x->v[i] = x->v[i-1];
	}
	x->v[mid] = y;
}

static void* flatmap_search(flatmap* x, void* y) {
	nukeif(!x);
	nukeif(!y);
	void* res;
	long long int start = 0;
	long long int end = x->len - 1;
	long long int mid = 0;
	while (start <= end) {
		mid = (start + end)/2;
		if (x->cmp(x->v[mid], y) < 0) {
			start = mid + 1;
			continue;
		}
		if (x->cmp(x->v[mid], y) == 0) {
			res = x->v[mid];
			return res;
		}
		if (x->cmp(x->v[mid], y) > 0) {
			end = mid - 1;
			continue;
		}
	}
	return NULL;
}

static void flatmap_kill(flatmap* x) {
	nukeif(!x);
	for (size_t i = 0; i < x->len; i++) {
		x->kill(x->v[i]);
	}
	if (x->v) checked_free(x->v);
}

static void flatmaps_push(flatmaps* x, size_t cap) {
	nukeif(!x);
	x->len++;
	if (x->len == 1) x->p = checked_malloc(sizeof(flatmap));
	else x->p = checked_realloc(x->p, sizeof(flatmap)*x->len);
	x->p[x->len-1] = flatmap_init(x->kill, x->cmp, cap);
}

static void flatmaps_free(flatmaps* x) {
	nukeif(!x);
	if (x->len == 0) return;
	x->len--;
	flatmap_kill(&x->p[x->len]);
	if (x->len)
		x->p = checked_realloc(x->p, sizeof(flatmap)*x->len);
	else
		checked_free(x->p);
}

void token_kill(void* n) {
	nukeif(!n);
	struct token* temp = n;
	if (temp->t == TOKEN_VAR) {
		var_clear(&temp->p.v);
	}
	if (temp->t != TOKEN_FUNCP) {
		checked_free(temp->k.p);
		checked_free(temp);
	}
}

__attribute__((hot))
int token_cmp(void* restrict l, void* r) {
	nukeif(!l);
	nukeif(!r);
	return constrcmp((*(struct token*)l).k, (*(struct token*)r).k);
}

struct lib {
	constr k;
	char* v;
};

void lib_kill(void* n) {
	nukeif(!n);
	struct lib* temp = n;
	checked_free(temp->k.p);
	checked_free(temp->v);
	checked_free(temp);
}

int lib_cmp(void* restrict l, void* r) {
	nukeif(!l);
	nukeif(!r);
	return constrcmp((*(struct lib*)l).k, (*(struct lib*)r).k);
}

static result lib_push(char* filename);
frame* f = NULL;
char* w = NULL;
flatmaps tokens = {
	.len = 0,
	.p = NULL,
	.kill = token_kill,
	.cmp = token_cmp
};
struct {
	size_t l;
	flatmap* t;
} t = {
	.l = 0,
	.t = NULL
};
flatmaps libs = {
	.len = 0,
	.p = NULL,
	.kill = lib_kill,
	.cmp = lib_cmp
};
flatmaps meths = {
	.len = 0,
	.p = NULL,
	.kill = meth_kill,
	.cmp = meth_cmp
};
var* args = NULL;
var* self = NULL;
clock_t start;
char* code_start;
const char* splash[] = {
	"Also try LISP!",
	"Totally not a text formatter!",
	"Technically error free!",
	"Technically type free!",
	"Technically lexer free!",
	"Who is John Galt?",
	"Initially called w++!",
	"Prototype made in Scratch!",
	"\"{{;{\" to crash!",
	"Single-pass!",
	"Recursive descent!",
	"LISP walked so walker could run!",
	"It walks on my machine!",
	"Don't point at me!",
	"Too dynamic 4 U!"
};

struct token core_pool[128];
size_t core_i = 0;
struct meth core_meth_pool[128];
size_t core_meth_i = 0;

static void core_funcp_place(const char* k, result (*fp)(void)) {
	nukeif(!k);
	struct token* temp = &core_pool[core_i];
	core_i++;
	temp->k.p = (char*)k;
	temp->k.len = checked_strlen(k);
	temp->t = TOKEN_FUNCP;
	temp->p.fp = fp;
	flatmap_insert(&t.t[t.l-1], temp);	
}

static void meth_funcp_place(const char* k, result (*meth)(var*)) {
	nukeif(!k);
	struct meth* temp = &core_meth_pool[core_meth_i];
	core_meth_i++;
	temp->k.p = (char*)k;
	temp->k.len = checked_strlen(k);
	temp->t = METHOD_FUNCP;
	temp->p.meth = meth;
	flatmap_insert(&meths.p[meths.len-1], temp);
}

static var* f_ref(void) {
	return &f->v;
}

__attribute__((hot))
static void var_stringify(var* v) {
	nukeif(!v);
	if (v->t == TYPE_STRING) return;
	string res;
	res.len = 0;
	res.cap = 16;
	res.p = checked_malloc(res.cap);
	size_t size;
	switch (v->t) {
		case TYPE_NUMBER:
			if (floor(v->v.n) == v->v.n) {
				goto integer;
				return;
			}
			size = snprintf(res.p, res.cap, "%lf", v->v.n);
			res.len = size;
			while (res.cap <= res.len + 1) res.cap*=2;
			res.p = checked_malloc(res.cap);
			snprintf(res.p, res.cap, "%lf", v->v.n);
			break;
		integer:
			v->t = TYPE_INTEGER;
			v->v.i = (long long int)v->v.n;
		case TYPE_INTEGER:
			size = snprintf(res.p, res.cap, "%lli", v->v.i);
			res.len = size;
			while (res.cap <= res.len + 1) res.cap*=2;
			res.p = checked_malloc(res.cap);
			snprintf(res.p, res.cap, "%lli", v->v.i);
			break;
		case TYPE_UINTEGER:
			size_t len = 1;
			size_t temp = v->v.u;
			while (temp > 9) {
				len++;
				temp /= 10;
			}
			res.len = len;
			while (res.cap <= res.len + 1) res.cap*=2;
			res.p = checked_malloc(res.cap);
			sprintf(res.p, "%ld", v->v.u);
			break;
		case TYPE_BOOLEAN:
			if (v->v.b) {
				res.p[0] = '1';
				res.len = 1;
			} else {
				res.p[0] = '0';
				res.len = 1;
			}
			break;
		case TYPE_ARRAY:
			arr_clear(v->v.a);
			checked_free(v->v.a);
		case TYPE_FUNCTION:
		case TYPE_EXPRESSION:
		case TYPE_MACRO:
		case TYPE_NONE:	
			break;
		default:
			__builtin_unreachable();
	}
	v->t = TYPE_STRING;
	v->v.s = checked_malloc(sizeof(string));
	*v->v.s = res;

}

static var var_froms(const char* s) {
	nukeif(!s);
	var res;
	res.t = TYPE_STRING;
	res.v.s = checked_malloc(sizeof(string));
	*res.v.s = string_from(s);
	return res;
}

static void var_toarr(var* v) {
	nukeif(!v);
	if (v->t == TYPE_ARRAY) return;
	var res;
	res.t = TYPE_ARRAY;
	res.v.a = checked_malloc(sizeof(arr));
	res.v.a->len = 1;
	res.v.a->cap = 8;
	res.v.a->p = checked_malloc(sizeof(var)*res.v.a->cap);
	res.v.a->p[0] = *v;
	*v = res;
}

__attribute__((hot))
static void f_push(void) {
	if (f == NULL) {
		f = checked_malloc(sizeof(frame));
		f->v.t = TYPE_NONE;
		f->v.v._ = NULL;
	}
	frame* res = checked_malloc(sizeof(frame));
	res->v.t = TYPE_NONE;
	res->v.v._ = NULL;
	res->n = f;
	f = res;
}

static void f_free(void) {
	var_clear(f_ref());
	frame* temp = f;
	f = f->n;
	checked_free(temp);
}

static string f_drops(void) {
	var_stringify(f_ref());
	string temp = *f->v.v.s;
	checked_free(f->v.v.s);
	f->v.t = TYPE_NONE;
	f->v.v._ = NULL;
	return temp;
}

static var f_drop(void) {
	var temp = f->v;
	f->v.t = TYPE_NONE;
	f->v.v._ = NULL;
	return temp;
}

static void f_assume(var s) {
	var_clear(f_ref());
	f->v = s;
}

static void f_sweep(void);

static void f_collapse(void) {
	switch(f->n->v.t) {
		case TYPE_NONE:
			f_sweep();
			return;
		default:
			var_stringify(f_ref());
			var_stringify(&f->n->v);
			string_cat(f->n->v.v.s, f->v.v.s);
			checked_free(f->v.v.s->p);
			f->v.v.s->p = NULL;
			checked_free(f->v.v.s);
			f->v.v.s = NULL;
			frame* temp = f;
			f = f->n;
			checked_free(temp);
			break;
	}
}

static void f_sweep(void) {
	var_clear(&f->n->v);
	frame* temp = f->n;
	f->n = f->n->n;
	checked_free(temp);
}

__attribute__((hot))
static void f_pushc(char c) {
	var_stringify(f_ref());
	string_pushc(f->v.v.s, c);
}

static void f_pushs(const char* s) {
	nukeif(!s);
	var_stringify(f_ref());
	string_pushs(f->v.v.s, s);
}

static void f_terminate(void) {
	var_stringify(f_ref());
	string_terminate(f->v.v.s);
}

static void f_replaces(const char* s) {
	var_clear(f_ref());
	f->v.t = TYPE_STRING;
	f->v.v.s = checked_malloc(sizeof(string));
	*f->v.v.s = string_from(s);
}

static void f_replacef(void) {
	var_clear(f_ref());
	f->v.t = TYPE_FUNCTION;
	f->v.v.f = w;
}

static void f_replacex(void) {
	var_clear(f_ref());
	f->v.t = TYPE_EXPRESSION;
	f->v.v.x = w;
}

static void f_replacem(void) {
	var_clear(f_ref());
	f->v.t = TYPE_MACRO;
	f->v.v.m = w;
}

static void f_replaceu(size_t n) {
	var_clear(f_ref());
	f->v.t = TYPE_UINTEGER;
	f->v.v.u = n;
}

static void f_replacei(long long int n);

static void f_replacen(double n) {
	var_clear(f_ref());
	f->v.t = TYPE_NUMBER;
	f->v.v.n = n;
}

static void f_replacei(long long int n) {
	var_clear(f_ref());
	f->v.t = TYPE_INTEGER;
	f->v.v.i = n;
}

static void var_clear(var* v) {
	nukeif(!v);
	switch (v->t) {
		case TYPE_STRING:
			checked_free(v->v.s->p);
			v->v.s->p = NULL;
			checked_free(v->v.s);
			v->v.s = NULL;
			break;
		case TYPE_ERROR:
			checked_free(v->v.e->p);
			v->v.e->p = NULL;
			checked_free(v->v.e);
			v->v.e = NULL;
			break;
		case TYPE_ARRAY:
			arr_clear(v->v.a);
			checked_free(v->v.a);
			v->v.a = NULL;
			break;
		default:
			break;
	}
	v->t = TYPE_NONE;
	v->v._ = NULL;
}

static char* f_refs(void) {
	var_stringify(f_ref());
	return f->v.v.s->p;
}

static constr f_refcs(void) {
	var_stringify(f_ref());
	return constr_from(*f->v.v.s);
}

static double f_num(void) {
	errno = 0;
	switch(f->v.t) {
		case TYPE_STRING:
			f_terminate();
			return s_tod(f_refs());
		case TYPE_NUMBER:
			return f->v.v.n;
		case TYPE_INTEGER:
			return (double)f->v.v.i;
		case TYPE_UINTEGER:
			return (double)f->v.v.u;
		case TYPE_BOOLEAN:
			return (double)f->v.v.b;
		default:
			return 0.0;
	}
}

static long long int f_int(void) {
	errno = 0;
	switch(f->v.t) {
		case TYPE_STRING:
			f_terminate();
			return s_toi(f_refs());
		case TYPE_NUMBER:
			return (long long int)f->v.v.n;
		case TYPE_INTEGER:
			return f->v.v.i;
		case TYPE_UINTEGER:
			return (long long int)f->v.v.u;
		case TYPE_BOOLEAN:
			return (long long int)f->v.v.b;
		default:
			return 0;
	}
}

static size_t f_uint(void) {
	errno = 0;
	switch(f->v.t) {
		case TYPE_STRING:
			f_terminate();
			return s_tou(f_refs());
		case TYPE_NUMBER:
			return (size_t)f->v.v.n;
		case TYPE_INTEGER:
			return (size_t)f->v.v.i;
		case TYPE_UINTEGER:
			return f->v.v.u;
		case TYPE_BOOLEAN:
			return (size_t)f->v.v.b;
		default:
			return 0;
	}
}

static bool f_bool(void) {
	switch(f->v.t) {
		case TYPE_STRING:
			f_terminate();
			char* end = f_refs();
			double res = strtod(f_refs(), &end);
			while ((*end == ' ' || *end == '\t') && *end != '\0') end++;
			if (*end == '\0') return res != 0;
			return 1;
		case TYPE_NUMBER:
			return f->v.v.n != 0;
		case TYPE_INTEGER:
			return f->v.v.i != 0;
		case TYPE_UINTEGER:
			return f->v.v.u != 0;
		case TYPE_BOOLEAN:
			return f->v.v.b;
		case TYPE_NONE:
			return 0;
		default:
			return 1;
	}
	
}

static void f_replaceb(bool x) {
	var_clear(f_ref());
	f->v.t = TYPE_BOOLEAN;
	f->v.v.b = x;
}

static bool f_eq(const char* s) {
	nukeif(!s);
	return constrcmp(f_refcs(), (constr) {.p = (char*)s, .len = checked_strlen(s)}) == 0;
}

static void t_push(void) {
	t.l++;
	if (t.l == 1) t.t = checked_malloc(sizeof(flatmap));
	else t.t = checked_realloc(t.t, sizeof(flatmap)*t.l);
	t.t[t.l-1] = flatmap_init(token_kill, token_cmp, t.l == 1 ? 64 : 8);
}

static void t_free(void) {
	t.l--;
	flatmap_kill(&t.t[t.l]);
	if (t.l)
		t.t = checked_realloc(t.t, sizeof(flatmap)*t.l);
	else
		checked_free(t.t);
}

static void* flatmaps_search(flatmaps* x, void* y) {
	nukeif(!x);
	void* res = NULL;
	size_t i = 0;
	do {
		i++;
		res = flatmap_search(&x->p[x->len-i], y);
	} while (res == NULL && i < x->len);
	return res;
}

static struct token* t_search(constr k) {
	struct token* res = NULL;
	size_t i = 0;
	struct token key = {.k = k};
	do {
		i++;
		res = flatmap_search(&t.t[t.l-i], (void*)&key);
	} while (res == NULL && i < t.l);
	return res;
}

static bool has_next(void) {
	return (*w != close && *w != '\0');
}

// FUNCTIONS

static result raw_parse(void);
static result parse_next(void);
static void raw_skip(void);
static void skip_next(void);
static void pure_next(void);
static result parse_token(void);
static result parse_var(var* obj);
static result expr_next(void);

static result parse_args(var* prev, var* new) {
	nukeif(!prev);
	nukeif(!new);
	args = new;
	args->t = TYPE_ARRAY;
	args->v.a = checked_malloc(sizeof(arr));
	args->v.a->len = 0;
	args->v.a->cap = 0;
	args->v.a->p = NULL;
	while (has_next()) {
		RES = parse_next();
		if (RES) {
			var_clear(args);
			args = prev;
			return RES;
		}
		arr_append(args->v.a, f_drop());
	}
	ok;
}

static result parse_token(void) {
	bool has_args = has_next();
	if (f->v.t == TYPE_FUNCTION || f->v.t == TYPE_MACRO || f->v.t == TYPE_EXPRESSION) {
		var v = f_drop();
		var* tempa = args;
		var newa;
		if (has_args) {
			tryor(parse_args(tempa, &newa), var_clear(&v));	
		} else args = NULL;
		if (v.t != TYPE_MACRO) {
			t_push();
			flatmaps_push(&libs, 0);
			flatmaps_push(&meths, 0);
		}
		char* temp = w;
		w = v.v.f;
		RES = (v.t == TYPE_EXPRESSION) ? expr_next() : parse_next();
		if (has_args) {
			var_clear(args);
		}
		args = tempa;
		w = temp;
		if (v.t != TYPE_MACRO) {
			t_free();
			flatmaps_free(&libs);
			flatmaps_free(&meths);
		}
		var_clear(&v);
		if (RES == RESULT_ERROR) return RES;
		ok;
	}
	if (f->v.t == TYPE_ARRAY) {
		var tempv = f_drop();
		tryor(parse_var(&tempv), var_clear(&tempv));
		if (f_ref()->t == TYPE_NONE) f_assume(tempv);
		else var_clear(&tempv);
		ok;
	}
	if (f_eq(code_start)) {
		f_replaces(splash[rand()%(sizeof(splash)/sizeof(char*))]);
		ok;
	}
	struct token* token = t_search(f_refcs());
	if (token == NULL) {
		string name = f_drops();
		if (!has_next()) {
			checked_free(name.p);
			ok;
		}
		struct token* temp = checked_malloc(sizeof(struct token));
		temp->k.p = name.p;
		temp->k.len = name.len;
		temp->t = TOKEN_FUNC;
		temp->p.f = w;
		flatmap_insert(&t.t[t.l-1], temp);
		ok;
	}
	if (token->t == TOKEN_FUNCP) {
		return token->p.fp();
	}
	if (token->t == TOKEN_FUNC) {
		var_clear(f_ref());
		var* tempa = args;
		var newa;
		if (has_args) {
			try(parse_args(tempa, &newa));	
		} else args = NULL;
		t_push();
		flatmaps_push(&libs, 0);
		flatmaps_push(&meths, 0);
		char* temp = w;
		w = token->p.f;
		RES = parse_next();
		if (has_args) {
			var_clear(args);
		}
		args = tempa;
		w = temp;
		t_free();
		flatmaps_free(&libs);
		flatmaps_free(&meths);
		if (RES == RESULT_ERROR) return RES;
		ok;
	}
	if (token->t == TOKEN_EXPR) {
		var_clear(f_ref());
		var* tempa = args;
		var newa;
		if (has_args) {
			try(parse_args(tempa, &newa));	
		} else args = NULL;
		t_push();
		flatmaps_push(&libs, 0);
		flatmaps_push(&meths, 0);
		char* temp = w;
		w = token->p.e;
		RES = expr_next();
		if (has_args) {
			var_clear(args);
		}
		args = tempa;
		w = temp;
		t_free();
		flatmaps_free(&libs);
		flatmaps_free(&meths);
		if (RES == RESULT_ERROR) return RES;
		ok;
	}
	if (token->t == TOKEN_MACRO) {
		var_clear(f_ref());
		var* tempa = args;
		var newa;
		if (has_args) {
			try(parse_args(tempa, &newa));	
		} else args = NULL;
		char* temp = w;
		w = token->p.m;
		RES = parse_next();
		if (has_args) {
			var_clear(args);
		}
		args = tempa;
		w = temp;
		if (RES == RESULT_ERROR) return RES;
		ok;
	}
	if (token->t == TOKEN_VAR) {
		try(parse_var(&token->p.v));
		ok;
	}
	__builtin_unreachable();
}

static result parse_var(var* obj) {
	nukeif(!obj);
	if (obj == NULL) {
		var_clear(f_ref());
		ok;
	}
	if (!has_next()) {
		var res = var_copy(*obj);
		f_assume(res);
		ok;
	}
	try(parse_next());
	bool has_args = has_next();
	if (f->v.t == TYPE_FUNCTION || f->v.t == TYPE_EXPRESSION || f->v.t == TYPE_MACRO) {
		var v = f_drop();
		var* tempa = args;
		var newa;
		if (has_args) {
			tryor(parse_args(tempa, &newa), var_clear(&v));	
		} else args = NULL;
		var* temps = self;
		self = obj;
		if (v.t != TYPE_MACRO) {
			t_push();
			flatmaps_push(&libs, 0);
			flatmaps_push(&meths, 0);
		}
		char* temp = w;
		w = v.v.f;
		RES = (v.t == TYPE_EXPRESSION) ? expr_next() : parse_next();
		if (has_args) {
			var_clear(args);
		}
		args = tempa;
		self = temps;
		w = temp;
		if (v.t != TYPE_MACRO) {
			t_free();
			flatmaps_free(&libs);
			flatmaps_free(&meths);
		}
		var_clear(&v);
		if (RES == RESULT_ERROR) return RES;
		ok;
	}
	size_t i = f_uint();
	if (errno == 0) {
		if (obj->t == TYPE_ARRAY) {
			if (i < obj->v.a->len) {
				try(parse_var(&obj->v.a->p[i]));
				ok;
			}
			var_clear(f_ref());
			ok;	
		}
		var_clear(f_ref());
		ok;
	}
	struct meth key = {.k = f_refcs()};
	struct meth* method = flatmaps_search(&meths, (void*)&key);
	if (method == NULL) {
		var_clear(f_ref());
		ok;
	}
	if (method->t == METHOD_FUNCP)
		return method->p.meth(obj);
	if (method->t == METHOD_FUNC) {
		var* tempa = args;
		var newa;
		if (has_args) {
			try(parse_args(tempa, &newa));
		} else args = NULL;
		var* temps = self;
		self = obj;
		t_push();
		flatmaps_push(&libs, 0);
		char* temp = w;
		w = method->p.f;
		RES = parse_next();
		if (has_args) {
			var_clear(args);
		}
		args = tempa;
		self = temps;
		w = temp;
		t_free();
		flatmaps_free(&libs);
		if (RES == RESULT_ERROR) return RES;
		ok;
	}
	__builtin_unreachable();
}

static result raw_parse(void) {
	char* start;
	while (*w != '\0') {
		if (*w == next || *w == close) ok;
		if (*w == open) {
			start = w;	
			w++;
			f_push();
			RES = raw_parse();
			if (RES) {
				f_sweep();
				w = start;
				while (has_next()) skip_next();
				return RES;
			}
			RES = parse_token();
			if (RES) {
				f_sweep();
				w = start;
				while (has_next()) skip_next();
				return RES;
			}
			while (has_next()) skip_next();
			f_collapse();
			if (*w != '\0') w++;
			continue;	
		}
		if (*w == '\\') {
			w++;
			if (*w == '\0') ok;
		}
		f_pushc(*w);
		w++;
	}
	ok;
}

static void raw_skip(void) {
	size_t nest = 1;
	while (*w != '\0') {
		if (*w == close) nest--;
		if (*w == next && nest == 1) nest--;
		if (*w == open) {
			nest++;
		}
		if (*w == '\\') {
			w++;
			if (*w == '\0') return;
		}
		if (!nest) return;
		w++;
	}

}

static void skip_next(void) {
	if (has_next()) {
		w++;
		raw_skip();
	}
}

static result parse_next(void) {
	var_clear(f_ref());
	if (has_next()) {
		w++;
		try(raw_parse());
	}
	ok;
}

static void raw_pure(void) {
	size_t nest = 1;
	while (*w != '\0') {
		if (*w == close) nest--;
		if (*w == next && nest == 1) nest--;
		if (*w == open) {
			nest++;
		}
		if (*w == '\\') {
			w++;
			if (*w == '\0') return;
		}
		if (!nest) return;
		f_pushc(*w);
		w++;
	}

}

static void pure_next(void) {
	var_clear(f_ref());
	if (has_next()) {	
		w++;
		raw_pure();
	}
}

static result raw_expr(int l) {
	bool q = false;
	double leftd, rightd;
	long long int lefti, righti;
	constr left;
	while (*w != '\0') {
		switch(*w) {
		case '"':
			q = !q;
			w++;
			if (*w == '\0') ok;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '.':
			f_pushc(*w);
			w++;
			break;
		case expropen:
			if (q) goto base;
			w++;
			f_push();
			tryor(raw_expr(9), f_sweep());
			f_collapse();
			break;
		case '!':
			if (q) goto base;
			w++;
			if (*w == '=') {
				if (l < 4) {
					w--;
					ok;
				}
				w++;
				f_terminate();
				left = constr_from(f_drops());
				tryor(raw_expr(4), checked_free(left.p));
				f_replaceb(s_tod(left.p) != f_num() || constrcmp(f_refcs(), left) != 0);
				checked_free(left.p);
				break;
			}
			f_push();
			tryor(raw_expr(0), f_sweep());
			f_replaceb(!f_bool());
			f_collapse();
			break;
		case '~':
			if (q) goto base;
			w++;
			f_push();
			tryor(raw_expr(0), f_sweep());
			f_replacei(~f_int());
			f_collapse();
			break;
		case '*':
			if (q) goto base;
			leftd = f_num();
			w++;
			if (*w == '*') {
				w++;
				var_clear(f_ref());
				try(raw_expr(0));
				rightd = f_num();
				f_replacen(pow(leftd, rightd));
				break;
			}
			if (l < 1) {
				w--;
				ok;
			}
			var_clear(f_ref());
			try(raw_expr(1));
			rightd = f_num();
			f_replacen(leftd * rightd);
			break;
		case '/':
			if (q) goto base;
			leftd = f_num();
			w++;
			if (l < 1) {
				w--;
				ok;
			}
			var_clear(f_ref());
			try(raw_expr(1));
			rightd = f_num();
			f_replacen(leftd / rightd);
			break;
		case '%':
			if (q) goto base;
			leftd = f_num();
			w++;
			if (l < 1) {
				w--;
				ok;
			}
			var_clear(f_ref());
			try(raw_expr(1));
			rightd = f_num();
			f_replacen(fmod(leftd, rightd));
			break;
		case '+':
			if (q) goto base;
			leftd = f_num();
			w++;
			if (l < 2) {
				w--;
				ok;
			}
			var_clear(f_ref());
			try(raw_expr(2));
			rightd = f_num();
			f_replacen(leftd + rightd);
			break;
		case '-':
			if (q) goto base;
			leftd = f_num();
			w++;
			if (l < 2) {
				w--;
				ok;
			}
			var_clear(f_ref());
			try(raw_expr(2));
			rightd = f_num();
			f_replacen(leftd - rightd);
			break;
		case '=':
			if (q) goto base;
			w++;
			if (*w == '<') {
				goto lessoreq;
			}
			if (*w == '>') {
				goto moreoreq;
			}
			if (l < 4) {
				w--;
				ok;
			}
			f_terminate();
			left = constr_from(f_drops());
			tryor(raw_expr(4), checked_free(left.p));
			f_replaceb(f_num() == s_tod(left.p) || constrcmp(f_refcs(), left) == 0);
			checked_free(left.p);
			break;
		case '<':
			if (q) goto base;
			w++;
			if (*w == '<') {
				if (l < 3) {
					w--;
					ok;
				}
				w++;
				lefti = f_int();
				var_clear(f_ref());
				try(raw_expr(8));
				righti = f_int();
				f_replacei(lefti<<righti);
				break;
			}
			if (*w == '=') {
			lessoreq:
				if (l < 4) {
					w--;
					ok;
				}
				w++;
				f_terminate();
				left = constr_from(f_drops());
				tryor(raw_expr(4), checked_free(left.p));
				f_replaceb(s_tod(left.p) <= f_num() || constrcmp(left, f_refcs()) <= 0);
				checked_free(left.p);
				break;
			}
			if (l < 4) {
				w--;
				ok;
			}
			f_terminate();
			left = constr_from(f_drops());
			tryor(raw_expr(4), checked_free(left.p));
			f_replaceb(s_tod(left.p) < f_num() || constrcmp(left, f_refcs()) < 0);
			checked_free(left.p);
			break;
		case '>':
			if (q) goto base;
			w++;
			if (*w == '>') {
				if (l < 3) {
					w--;
					ok;
				}
				w++;
				lefti = f_int();
				var_clear(f_ref());
				try(raw_expr(8));
				righti = f_int();
				f_replacei(lefti>>righti);
				break;
			}
			if (*w == '=') {
			moreoreq:
				if (l < 4) {
					w--;
					ok;
				}
				w++;
				f_terminate();
				left = constr_from(f_drops());
				tryor(raw_expr(4), checked_free(left.p));
				f_replaceb(s_tod(left.p) >= f_num() || constrcmp(left, f_refcs()) >= 0);
				checked_free(left.p);
				break;
			}
			if (l < 4) {
				w--;
				ok;
			}
			f_terminate();
			left = constr_from(f_drops());
			tryor(raw_expr(4), checked_free(left.p));
			f_replaceb(s_tod(left.p) > f_num() || constrcmp(left, f_refcs()) > 0);
			checked_free(left.p);
			break;
		case '&':
			if (q) goto base;
			lefti = f_int();
			w++;
			if (*w == '&') {
				if (l < 8) {
					w--;
					ok;
				}
				w++;
				var_clear(f_ref());
				try(raw_expr(8));
				righti = f_int();
				f_replaceb(lefti&&righti);
				break;
			}
			if (l < 5) {
				w--;
				ok;
			}
			var_clear(f_ref());
			try(raw_expr(5));
			righti = f_int();
			f_replacei(lefti & righti);
			break;
		case '^':
			if (q) goto base;
			lefti = f_int();
			w++;
			if (l < 6) {
				w--;
				ok;
			}
			var_clear(f_ref());
			try(raw_expr(6));
			righti = f_int();
			f_replacei(lefti ^ righti);
			break;
		case '|':
			if (q) goto base;
			lefti = f_int();
			w++;
			if (*w == '|') {
				if (l < 9) {
					w--;
					ok;
				}
				w++;
				var_clear(f_ref());
				try(raw_expr(9));
				righti = f_int();
				f_replaceb(lefti||righti);
				break;
			}
			if (l < 7) {
				w--;
				ok;
			}
			var_clear(f_ref());
			try(raw_expr(7));
			righti = f_int();
			f_replacei(lefti | righti);
			break;
		case exprclose:
			if (q) goto base;
			w++;
			ok;
		case next:
		case close:
			if (q) goto base;
			ok;
		case open:	
			if (q) goto base;
			w++;
			f_push();
			tryor(raw_parse(), f_sweep());
			tryor(parse_token(), f_sweep());
			while(*w != close && *w != '\0') skip_next();
			w++;
			f_collapse();
			break;
		case '\\':
			w++;
			if (*w == '\0') ok;
		base:
		default:
			if (q) f_pushc(*w);
			w++;
			break;
		}
	}
	ok;
}

static result expr_next(void) {
	var_clear(f_ref());
	if (has_next()) {
		w++;
		try(raw_expr(9));
	}
	ok;
}

static result lib_push(char* filename) {
	nukeif(!filename);
	char* temp = w;
	struct lib* library = checked_malloc(sizeof(struct lib));
	library->k.p = filename;
	library->k.len = checked_strlen(filename);
	struct lib* search = flatmaps_search(&libs, library);
	if (search) {
		checked_free(filename);
		ok;
	}
	FILE* file = fopen(filename, "r");
	if (file == NULL) {
		checked_free(filename);
		ok;
	}
	fseek(file, 0L, SEEK_END);
	size_t size = ftell(file) + 1;
	rewind(file);
	library->v = checked_malloc(size);
	fread(library->v, 1, size, file);
	if (library->v[size-2] == '\n') library->v[size-2] = '\0';
	fclose(file);
	flatmap_insert(&libs.p[libs.len-1], library);
	w = library->v;
	char* code_temp = code_start;
	code_start = library->v;
	RES = raw_parse();
	code_start = code_temp;
	w = temp;
	return RES;
}

// CORE LIBRARY

result walker_w(void) {
	try(parse_next());
	bool has_args = has_next();
	f_terminate();
	string code = f_drops();
	var* tempa = args;
	var newa;
	if (has_args) {
		try(parse_args(tempa, &newa));	
	} else args = NULL;
	t_push();
	flatmaps_push(&libs, 0);
	flatmaps_push(&meths, 0);
	char* temp = w;
	char* code_temp = code_start;
	code_start = code.p;
	w = code.p;
	RES = raw_parse();
	if (has_args) {
		var_clear(args);
	}
	args = tempa;
	w = temp;
	code_start = code_temp;
	checked_free(code.p);
	t_free();
	flatmaps_free(&libs);
	flatmaps_free(&meths);
	if (RES == RESULT_ERROR) return RES;
	ok;
}

result walker_sandboxed_w(void) {
	ok;
}

result walker_self(void) {
	try(parse_var(self));
	ok;
}

result walker_literal_method(void) {
	try(parse_next());
	var tempv = f_drop();
	tryor(parse_var(&tempv), var_clear(&tempv));
	if (f_ref()->t == TYPE_NONE) f_assume(tempv);
	else var_clear(&tempv);
	ok;
}

result walker_args(void) {
	try(parse_var(args));
	ok;
}

result walker_rename(void) {
	try(parse_next());
	string old = f_drops();
	tryor(parse_next(), checked_free(old.p));
	string new = f_drops();
	rename(old.p, new.p);
	checked_free(old.p);
	checked_free(new.p);
	ok;
}

result walker_return(void) {
	try(parse_next());
	return RESULT_RETURN;
}

result walker_include(void) {
	try(parse_next());
	f_terminate();
	try(lib_push(f_drops().p));
	ok;
}

result walker_token(void) {
	try(parse_next());
	struct token* token = t_search(f_refcs());
	if (token == NULL) {
		f_replaces("undefined");
		ok;
	}
	switch(token->t) {
		case TOKEN_FUNCP:
			f_replaces("language function");
			ok;
		case TOKEN_FUNC:
			f_replaces("function");
			ok;
		case TOKEN_EXPR:
			f_replaces("expression");
			ok;
		case TOKEN_MACRO:
			f_replaces("macro");
			ok;
		case TOKEN_VAR:
			f_replaces("variable");
			ok;
		default:
			f_replaces("undefined");
			ok;
	}
	__builtin_unreachable();
}

result walker_error(void) {
	try(parse_next());
	if (f->v.t != TYPE_ERROR) {
		var_stringify(f_ref());
		f->v.t = TYPE_ERROR;
	}
	ok;
}

result walker_throw(void) {
	try(parse_next());
	if (f->v.t != TYPE_ERROR) {
		var_stringify(f_ref());
		f->v.t = TYPE_ERROR;
	}
	return RESULT_ERROR;
}

result walker_try(void) {
	RES = parse_next();
	if (RES == RESULT_RETURN) return RES;
	if (f->v.t == TYPE_ERROR) {
		f->v.t = TYPE_STRING;
	}
	ok;
}

result walker_type(void) {
	try(parse_next());
	switch(f->v.t) {
		case TYPE_NONE:       f_replaces("none"); ok;
		case TYPE_STRING:     f_replaces("string"); ok;
		case TYPE_NUMBER:     f_replaces("number (f64)"); ok;
		case TYPE_INTEGER:    f_replaces("integer (i64)"); ok;
		case TYPE_UINTEGER:   f_replaces("unsigned integer (u64)"); ok;
		case TYPE_BOOLEAN:    f_replaces("boolean"); ok;
		case TYPE_FUNCTION:   f_replaces("function"); ok;
		case TYPE_EXPRESSION: f_replaces("expression"); ok;
		case TYPE_MACRO:      f_replaces("macro");
		case TYPE_ERROR:      f_replaces("error (string)");ok;
		case TYPE_ARRAY:      f_replaces("array"); ok;
		default:              __builtin_unreachable();
	}
	ok;
}

result walker_open(void) {
	var_clear(f_ref());
	f_pushc(open);
	ok;
}

result walker_next(void) {
	var_clear(f_ref());
	f_pushc(next);
	ok;
}

result walker_close(void) {
	var_clear(f_ref());
	f_pushc(close);
	ok;
}

result walker_copy(void) {
	string temp;
	try(parse_next());
	size_t i = f_uint();
	try(parse_next());
	f_terminate();
	temp = f_drops();
	while (i--) {
		f_pushs(temp.p);
	}
	checked_free(temp.p);
	ok;
}

result walker_repeat(void) {	
	try(parse_next());
	char* temp = w;
	size_t i = f_uint();
	var_clear(f_ref());
	while(i--) {
		f_push();
		w = temp;
		tryor(parse_next(), f_sweep());
		f_collapse();
	}
	ok;
}

result walker_while(void) {	
	char* temp = w;
	var_clear(f_ref());
again:
	w = temp;
	try(expr_next());
	if (f_bool()) {
		try(parse_next());
		f_collapse();
		f_push();
		goto again;
	}
	ok;
}

result walker_do_while(void) {	
	char* temp = w;
	var_clear(f_ref());
again:
	w = temp;
	try(parse_next());
	f_collapse();
	f_push();
	try(expr_next());
	if (f_bool()) {
		goto again;
	}
	ok;
}

result walker_length(void) {	
	try(parse_next());
	var_stringify(f_ref());
	f_replaceu(f->v.v.s->len);
	ok;
}

result walker_newline(void) {
	f_replaces("\n");
	ok;
}

result walker_if(void) {
	try(expr_next());
	if (f_bool()) {
		try(parse_next());
		ok;
	}
	skip_next();
	try(parse_next());
	ok;
}

result walker_print(void) {
	while (has_next()) {
		try(parse_next());
		var_stringify(f_ref());
		f_terminate();
		fputs(f_refs(), stdout);
	}
	var_clear(f_ref());
	ok;
}

result walker_println(void) {
	while (has_next()) {
		try(parse_next());
		var_stringify(f_ref());
		f_terminate();
		puts(f_refs());
	}
	var_clear(f_ref());
	ok;
}

result walker_scan(void) {
	fflush(stdout);
	var_clear(f_ref());
	char temp;
	while ((temp = getchar()) != ' ' && temp != '\t' && temp != '\n' && temp != EOF)
		f_pushc(temp);
	ok;
}

result walker_scanln(void) {
	fflush(stdout);
	var_clear(f_ref());
	char temp;
	while ((temp = getchar()) != '\n' && temp != EOF)
		f_pushc(temp);
	ok;
}

result walker_scanc(void) {
	var_clear(f_ref());
	char temp = getchar();
	if (temp != EOF) f_pushc(temp);
	f_terminate();
	ok;
}

result walker_eq(void) {
	constr temp;
	try(parse_next());
	f_terminate();
	temp = constr_from(f_drops());
	while (has_next()) {
		tryor(parse_next(), checked_free(temp.p));
		if (f_num() != s_tod(temp.p) || constrcmp(f_refcs(), temp) != 0) {
			f_replaceb(false);
			checked_free(temp.p);
			ok;
		}
	}
	f_replaceb(true);
	checked_free(temp.p);
	ok;
}

result walker_noneq(void) {
	constr temp;
	try(parse_next());
	f_terminate();
	temp = constr_from(f_drops());
	while (has_next()) {
		tryor(parse_next(), checked_free(temp.p));
		if (f_num() == s_tod(temp.p) || constrcmp(f_refcs(), temp) == 0) {
			f_replaceb(false);
			checked_free(temp.p);
			ok;
		}
	}
	f_replaceb(true);
	checked_free(temp.p);
	ok;
}

result walker_bool(void) {
	try(parse_next());
	f_replaceb(f_bool());
	ok;
}

result walker_not(void) {
	try(parse_next());
	f_replaceb(!f_bool());
	ok;
}

result walker_add(void) {
	double res = 0;
	while (has_next()) {
		try(parse_next());
		res += f_num();
	}
	f_replacen(res);
	ok;
}

result walker_sub(void) {
	double res = 0, temp;
	try(parse_next());
	res = f_num();
	while (has_next()) {
		try(parse_next());
		temp = f_num();
		res -= temp;
	}
	f_replacen(res);
	ok;
}

result walker_mul(void) {
	double res = 0, temp;
	try(parse_next());
	res = f_num();
	while (has_next()) {
		try(parse_next());
		temp = f_num();
		res *= temp;
	}
	f_replacen(res);
	ok;
}

result walker_div(void) {
	double res = 0, temp;
	try(parse_next());
	res = f_num();
	while (has_next()) {
		try(parse_next());
		temp = f_num();
		res /= temp;
	}
	f_replacen(res);
	ok;
}

result walker_num(void) {
	try(parse_next());
	f_replacen(f_num());
	ok;
}

result walker_int(void) {
	try(parse_next());
	f_replacei(f_int());
	ok;
}

result walker_uint(void) {
	try(parse_next());
	f_replacei(f_int());
	ok;
}

result walker_let(void) {
	string name;
	var value;
	try(parse_next());
	name = f_drops();
	tryor(parse_next(), checked_free(name.p));
	value = f_drop();
	struct token* token = (struct token*)t_search(constr_from(name));
	if (token && token->t == TOKEN_FUNCP) {
		checked_free(name.p);
		var_clear(&value);
		ok;
	}
	struct token* temp = checked_malloc(sizeof(struct token));
	temp->k = constr_from(name);
	temp->t = TOKEN_VAR;
	temp->p.v = value;
	flatmap_insert(&t.t[t.l-1], temp);
	ok;
}

result walker_new_file(void) {
	ok;
}

result walker_open_file(void) {
	ok;
}
/*
result walker_new_bin(void) {
	ok;
}

result walker_open_bin(void) {
	ok;
}
*/
result walker_arr(void) {
	var res;
	res.t = TYPE_ARRAY;
	res.v.a = checked_malloc(sizeof(arr));
	res.v.a->len = 0;
	res.v.a->cap = 0;
	res.v.a->p = NULL;
	while (has_next()) {
		tryor(parse_next(), var_clear(&res));
		arr_append(res.v.a, f_drop());
	}
	f_assume(res);
	ok;
}

result walker_fun(void) {	
	string name;
	try(parse_next());
	name = f_drops();
	if (!has_next()) {
		checked_free(name.p);
		ok;
	}
	struct token* token = (struct token*)t_search(constr_from(name));
	if (token && token->t == TOKEN_FUNCP) {
		checked_free(name.p);
		ok;
	}
	struct token* temp = checked_malloc(sizeof(struct token));
	temp->k.p = name.p;
	temp->k.len = name.len;
	temp->t = TOKEN_FUNC;
	temp->p.f = w;
	flatmap_insert(&t.t[t.l-1], temp);
	ok;
}

result walker_def(void) {
	string name;
	try(parse_next());
	f_terminate();
	s_tou(f_refs());
	name = f_drops();
	if (!has_next()) {
		checked_free(name.p);
		ok;
	}
	struct token* token = (struct token*)t_search(constr_from(name));
	if (token && token->t == TOKEN_FUNCP) {
		checked_free(name.p);
		ok;
	}
	struct token* temp = checked_malloc(sizeof(struct token));
	temp->k.p = name.p;
	temp->k.len = name.len;
	temp->t = TOKEN_EXPR;
	temp->p.e = w;
	flatmap_insert(&t.t[t.l-1], temp);
	ok;
}

result walker_mac(void) {
	string name;
	try(parse_next());
	name = f_drops();
	if (!has_next()) {
		checked_free(name.p);
		ok;
	}
	struct token* token = (struct token*)t_search(constr_from(name));
	if (token && token->t == TOKEN_FUNCP) {
		checked_free(name.p);
		ok;
	}
	struct token* temp = checked_malloc(sizeof(struct token));
	temp->k.p = name.p;
	temp->k.len = name.len;
	temp->t = TOKEN_MACRO;
	temp->p.m = w;
	flatmap_insert(&t.t[t.l-1], temp);
	ok;
}

result walker_meth(void) {
	struct meth* key = checked_malloc(sizeof(struct meth));
	try(parse_next());
	key->k = constr_from(f_drops());
	if (!has_next()) {
		checked_free(key->k.p);
		checked_free(key);
		ok;
	}
	struct meth* method = flatmaps_search(&meths, (void*)key);
	if (method && method->t == METHOD_FUNC) {
		checked_free(key->k.p);
		checked_free(key);
		ok;
	}
	key->t = METHOD_FUNC;
	key->p.f = w;
	flatmap_insert(&meths.p[meths.len-1], key);
	ok;
}

result walker_pure(void) {
	pure_next();
	ok;
}

result walker_comment(void) {
	var_clear(f_ref());
	ok;
}

result walker_expr(void) {
	try(expr_next());
	ok;	
}

result walker_timer(void) {
	f_replacen((double)(clock()-start) / CLOCKS_PER_SEC);
	ok;
}
result walker_reset_timer(void) {
	start = clock();
	var_clear(f_ref());
	ok;
}

result walker_timestamp(void) {
	f_replaceu((size_t)time(NULL));
	ok;
}

result walker_rand(void) {
	f_replaceu(rand());
	ok;
}

result walker_letter(void) {
	try(parse_next());
	size_t temp = f_uint();
	if (errno != 0) {
		var_clear(f_ref());
		ok;
	}
	try(parse_next());
	var_stringify(f_ref());
	if (temp < f->v.v.s->len) {
		string res = f_drops();
		f_pushc(res.p[temp]);
		checked_free(res.p);
		ok;
	}
	var_clear(f_ref());
	ok;
}

result walker_string(void) {
	try(parse_next());
	var_stringify(f_ref());
	ok;
}

result walker_sin(void) {
	try(parse_next());
	f_replacen(sin(f_num()));
	ok;
}

result walker_quine(void) {
	f_replaces(code_start);
	ok;
}

result walker_f(void) {
	if (!has_next()) {
		var_clear(f_ref());
		ok;
	}
	f_replacef();
	ok;
}

result walker_x(void) {
	if (!has_next()) {
		var_clear(f_ref());
		ok;
	}
	f_replacex();
	ok;
}

result walker_m(void) {
	if (!has_next()) {
		var_clear(f_ref());
		ok;
	}
	f_replacem();
	ok;
}

__attribute__((cold))
static inline void place_core(void) {
	// misc
	core_funcp_place("timer", walker_timer);
	core_funcp_place("reset timer", walker_reset_timer);
	core_funcp_place("timestamp", walker_timestamp);
	core_funcp_place("rand", walker_rand);
	core_funcp_place("copy", walker_copy);
	core_funcp_place("length", walker_length);

	// control
	core_funcp_place("if", walker_if);
	// switch
	core_funcp_place("repeat", walker_repeat);
	// for
	core_funcp_place("while", walker_while);
	core_funcp_place("do-while", walker_do_while);

	// io
	core_funcp_place("print", walker_print);
	core_funcp_place("println", walker_println);
	core_funcp_place("scan", walker_scan);
	core_funcp_place("scanln", walker_scanln);
	core_funcp_place("scanc", walker_scanc);
	
	// string
	core_funcp_place("newline", walker_newline);
	core_funcp_place("letter", walker_letter);
	// substring

	// logic
	core_funcp_place("=", walker_eq);
	core_funcp_place("eq", walker_eq);
	core_funcp_place("!=", walker_noneq);
	// >
	// >= =>
	// <
	// <= =<
	// && and
	// &
	// || or
	// |
	// ^ xor
	// <<
	// >>
	core_funcp_place("not", walker_not);
	core_funcp_place("!", walker_not);
	// hex

	// math
	core_funcp_place("+", walker_add);
	core_funcp_place("-", walker_sub);
	core_funcp_place("*", walker_mul);
	core_funcp_place("/", walker_div);
	// mod
	// sin
	// cos
	// tan
	// asin
	// acos
	// atan
	// ** pow
	// sqrt
	// qbrt
	// log2
	// log10
	// ln
	// abs
	// e
	// pi
	// floor
	// ceil
	// round
	// trunc
	
	// type
	core_funcp_place("type", walker_type);
	core_funcp_place("bool", walker_bool);
	core_funcp_place("num", walker_num);
	core_funcp_place("f64", walker_num);
	core_funcp_place("int", walker_int);
	core_funcp_place("i64", walker_int);
	core_funcp_place("uint", walker_uint);
	core_funcp_place("u64", walker_uint);
	core_funcp_place("string", walker_string);
	core_funcp_place("error", walker_error);

	// meta
	core_funcp_place("token", walker_token);
	core_funcp_place("let", walker_let);
	core_funcp_place("arr", walker_arr);
	core_funcp_place("fun", walker_fun);
	core_funcp_place("f", walker_f);
	core_funcp_place("def", walker_def);
	core_funcp_place("x", walker_x);
	core_funcp_place("mac", walker_mac);
	core_funcp_place("m", walker_m);
	core_funcp_place("met", walker_meth);
	core_funcp_place("quine", walker_quine);
	core_funcp_place("rename", walker_rename);
	core_funcp_place("w", walker_w);
	// new file x (fopen x w+)
	// open file x (fopen x r+)
	// new binary x (fopen x wb+)
	// open binary x (fopen x rb+)
	// x read
	// x readln
	// x readc
	// x readb
	// x write str
	// x writeln str
	// x clear
	// x end
	// x start
	// x last str
	// x next str
	// x to i
	core_funcp_place("#", walker_comment);
	core_funcp_place("pure", walker_pure);
	core_funcp_place("ex", walker_expr);
	core_funcp_place("return", walker_return);
	core_funcp_place("open", walker_open);
	core_funcp_place("next", walker_next);
	core_funcp_place("close", walker_close);
	core_funcp_place("include", walker_include);
	core_funcp_place("throw", walker_throw);
	core_funcp_place("try", walker_try);
	core_funcp_place("self", walker_self);
	core_funcp_place("args", walker_args);
	core_funcp_place("$", walker_literal_method);

#ifdef DEBUG
	printf("%lu functions placed\n", core_i);
#endif	
}

result meth_assign(var* v) {
	try(parse_next());
	var_clear(v);
	*v = f_drop();
	ok;
}

result meth_type(var* v) {
	switch(v->t) {
		case TYPE_NONE:       f_replaces("none"); ok;
		case TYPE_STRING:     f_replaces("string"); ok;
		case TYPE_ERROR:      f_replaces("error (string)"); ok;
		case TYPE_NUMBER:     f_replaces("number (f64)"); ok;
		case TYPE_INTEGER:    f_replaces("integer (i64)"); ok;
		case TYPE_UINTEGER:   f_replaces("unsigned integer (u64)"); ok;
		case TYPE_BOOLEAN:    f_replaces("boolean"); ok;
		case TYPE_FUNCTION:   f_replaces("function"); ok;
		case TYPE_EXPRESSION: f_replaces("expression"); ok;
		case TYPE_MACRO:      f_replaces("macro"); ok;
		case TYPE_ARRAY:      f_replaces("array"); ok;
		default:              __builtin_unreachable();
	}
	ok;
}

result meth_length(var* v) {
	if (v->t != TYPE_ARRAY) {
		f_replaceu(0);
		ok;
	}
	f_replaceu(v->v.a->len);
	ok;
}

result meth_append(var* v) {
	var_clear(f_ref());
	var_toarr(v);
	while (has_next()) {
		try(parse_next());
		arr_append(v->v.a, f_drop());
	}
	ok;
}

result meth_insert(var* v) {
	var_clear(f_ref());
	try(parse_next());
	size_t n = f_uint();
	if (errno != 0) {
		var_clear(f_ref());
		ok;
	}
	var_toarr(v);
	try(parse_next());
	arr_insert(v->v.a, n, f_drop());
	ok;
}

result meth_call(var* v) {
	if (v->t == TYPE_FUNCTION || v->t == TYPE_EXPRESSION || v->t == TYPE_MACRO) {
		bool has_args = has_next();
		var_clear(f_ref());
		var* tempa = args;
		var newa;
		if (has_args) {
			try(parse_args(tempa, &newa));	
		} else args = NULL;
		if (v->t != TYPE_MACRO) {
			t_push();
			flatmaps_push(&libs, 0);
			flatmaps_push(&meths, 0);
		}
		char* temp = w;
		w = v->v.f;
		RES = (v->t == TYPE_EXPRESSION) ? expr_next() : parse_next();
		if (has_args) {
			var_clear(args);
		}
		args = tempa;
		w = temp;
		if (v->t != TYPE_MACRO) {
			t_free();
			flatmaps_free(&libs);
			flatmaps_free(&meths);
		}
		if (RES == RESULT_ERROR) return RES;
		ok;	
	}
	var_clear(f_ref());
	ok;
}

result meth_clear(var* v) {
	var_clear(f_ref());
	var_clear(v);
	v->t = TYPE_ARRAY;
	v->v.a = checked_malloc(sizeof(arr));
	v->v.a->len = 0;
	v->v.a->cap = 0;
	v->v.a->p = NULL;
	ok;
}

result meth_remove(var* v) {
	if (v->t != TYPE_ARRAY) {
		var_clear(f_ref());
		ok;
	}
	try(parse_next());
	size_t i = f_uint();
	if (errno != 0) {
		var_clear(f_ref());
		ok;
	}
	arr_remove(v->v.a, i);
	var_clear(f_ref());
	ok;
}

result meth_pop(var* v) {
	if (v->t != TYPE_ARRAY) {
		var_clear(f_ref());
		ok;
	}
	try(parse_next());
	size_t i = f_uint();
	if (errno != 0) {
		var_clear(f_ref());
		ok;
	}
	f_assume(arr_pop(v->v.a, i));
	ok;
}

result meth_indexof(var* v) {
	if (v->t != TYPE_ARRAY) {
		f_replacei(-1);
		ok;
	}
	try(parse_next());
	constr comp = f_refcs();
	var temp;
	for (size_t i = 0; i < v->v.a->len; i++) {
		temp = var_copy(v->v.a->p[i]);
		var_stringify(&temp);
		if (constrcmp(constr_from(*temp.v.s), comp) == 0) {
			f_replacei(i);
			var_clear(&temp);
			ok;
		}
		var_clear(&temp);
	}
	f_replacei(-1);
	ok;
}

result meth_inc(var* v) {
	var_clear(f_ref());
	switch(v->t) {
		case TYPE_NONE:
			v->t = TYPE_NUMBER;
			v->v.n = 1.0;
			ok;
		case TYPE_STRING:
			string_terminate(v->v.s);
			double num = s_tod(v->v.s->p);
			checked_free(v->v.s->p);
			checked_free(v->v.s);
			v->t = TYPE_NUMBER;
			v->v.n = num + 1.0;
			ok;
		case TYPE_NUMBER:
			v->v.n++;
			ok;
		case TYPE_INTEGER:
			v->v.i++;
			ok;
		case TYPE_UINTEGER:
			v->v.u++;
			ok;
		case TYPE_BOOLEAN:
			v->v.b = !v->v.b;
			ok;
		default:
			ok;
	}
}

__attribute__((cold))
static inline void place_core_meth(void) {
	meth_funcp_place("=", meth_assign);
	// +=
	meth_funcp_place("++", meth_inc);	
	// -=
	// --
	// *=
	// /=
	// %=
	// >>=
	// <<=
	meth_funcp_place("call", meth_call);
	meth_funcp_place("clear", meth_clear);
	meth_funcp_place("append", meth_append);
	meth_funcp_place("insert", meth_insert);
	meth_funcp_place("pop", meth_pop);
	meth_funcp_place("remove", meth_remove);
	meth_funcp_place("indexof", meth_indexof);
	meth_funcp_place("length", meth_length);
	// extend
	// range
	// to string
	// replace
	// replace all
	meth_funcp_place("type", meth_type);

#ifdef DEBUG
	printf("%lu methods placed\n", core_meth_i);
#endif
}

// MAIN

int main(int argc, char* argv[], char* envp[]) {
	start = clock();
	if (argc == 1) {
		printf("WALKER\n");
		printf("It walks.\n");
		return 0;
	}
	if (argc == 2) {
		printf("No input given\n");
		printf("w f [PATH] [ARGUMENTS]\n");
		printf("w e [CODE] [ARGUMENTS]\n");
		printf("w i [ARGUMENTS]\n");
		return 0;
	}
	f_push();
	flatmaps_push(&libs, 8);
	flatmaps_push(&meths, 64);
	t_push();
	string code;
	code.p = NULL;
	var newa;
	if (strcmp(argv[1], "f") == 0) {
		is_file = true;
		FILE* file = fopen(argv[2], "r");
		if (file == NULL) {
			printf("Failed to open file\n");
			printf("w f [PATH] [ARGUMENTS]\n");
			printf("w e [CODE] [ARGUMENTS]\n");
			printf("w i [ARGUMENTS]\n");
			return 0;
		}
		fseek(file, 0L, SEEK_END);
		size_t size = ftell(file) + 1;
		rewind(file);
		code.len = size;
		code.cap = size;
		code.p = checked_malloc(size);
		fread(code.p, 1, size, file);
		if (code.p[size-2] == '\n') code.p[size-2] = '\0';
		w = code.p;
		code_start = code.p;
		fclose(file);
	} else if (strcmp(argv[1], "e") == 0) {
		w = argv[2];
		code_start = argv[2];
	} else if (strcmp(argv[1], "i")) {
		
	} else {
		f_free();
		t_free();
		flatmaps_free(&libs);
		flatmaps_free(&meths);
		printf("No such option\n");
		printf("w f [PATH] [ARGUMENTS]\n");
		printf("w e [CODE] [ARGUMENTS]\n");
		printf("w i [ARGUMENTS]\n");
		return 0;
	}
	if (argc-3 > 0) {
		args = &newa;
		args->t = TYPE_ARRAY;
		args->v.a = checked_malloc(sizeof(arr));
		args->v.a->len = 0;
		args->v.a->cap = 0;
		args->v.a->p = NULL;
		for (int i = 0; i < argc-3; i++) {
			arr_append(args->v.a, var_froms(argv[i+3]));
		}
	}
	place_core();
	place_core_meth();
	raw_parse();
	
	if (!is_file) {
		var_stringify(f_ref());
		f_terminate();
		printf("%s\n", f->v.v.s->p);
	} else {
		checked_free(code.p);
	}
	if (argc-3 > 0) {
		var_clear(args);
	}
	f_free();
	flatmaps_free(&libs);
	flatmaps_free(&meths);
	t_free();
	return 0;
}
