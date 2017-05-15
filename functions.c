#include <editline/readline.h>
#include <editline/history.h>
#include "mpc.h"
#include "FUNCS.h"

#define LASSERT(args, cond, fmt, ...) \
	if (!(cond)) { \
		lval* err = lval_err(fmt, ##__VA_ARGS__); \
		lval_del(args); \
		return err; \
	}

#define LASSERT_TYPE(func, args, index, expect) \
	LASSERT(args, args->cell[index]->type == expect, \
	"Function '%s' passed incorrect type or argument %i. Got %s, expected %s.", \
	func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM(func, args, num) \
	LASSERT(args, args->count == num, \
	"Function '%s' passed incorrect number of arguments. Got %i, expected %i.", \
	func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
	LASSERT(args, args->cell[index]->count != 0, \
	"Function '%s' passed {} for argument %i.", func, index);

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUN };

/* enum2string */
char* ltype_name(int t) {
	switch(t) {
		case LVAL_NUM: return "Number";
		case LVAL_ERR: return "Error";
		case LVAL_SYM: return "Symbol"; 
		case LVAL_SEXPR: return "S-Expression";
		case LVAL_QEXPR: return "Q-Expression";
		case LVAL_FUN: return "Function";
		default: return "Unknown type";
	}
}


/* Constructors */

// lval constructor for numbers
lval* lval_num(long x) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_NUM;
	v->num = x;
	return v;
}

//lval constructor for errors
lval* lval_err(char* fmt, ...) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_ERR;
	
	/* Create va lis and intialize */
	va_list va;
	va_start(va, fmt);
	
	/* Allocate 512 bytes of space */
	v->err = malloc(512);
	
	/* Print up to 511 character of the error string to v->err */
	vsnprintf(v->err, 511, fmt, va);
	
	/* Reallocate to actual number of bytes used */
	v->err = realloc(v->err, strlen(v->err)+1);
	
	/* Clean up of va list */
	va_end(va);

	return v;
}

//lval constructor for symbol
lval* lval_sym(char* symbol) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_SYM;
	v->sym = malloc(strlen(symbol) + 1);
	strcpy(v->sym, symbol);
	return v;
}

//lval constructor for sexpr
lval* lval_sexpr(void) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_SEXPR;
	v->count = 0;
	v->cell = NULL;
	return v;
}

//lval constructor for qexpr
lval* lval_qexpr(void) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_QEXPR;
	v->count = 0;
	v->cell = NULL;
	return v;
}

//lval constructor for functions
lval* lval_fun(lbuiltin func) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_FUN;
	v->fun = func;
	return v;
}

//lval constructor for lambda
lval* lval_lambda(lval* formals, lval* body) {
	lval* v = malloc(sizeof(lval));
	
	v->builtin = NULL;
	
	v->env = lenv_new();
	
	v->formals = formals;
	v->body = body;
	return v;
}


//lenv constructor
lenv* lenv_new(void) {
	lenv* e = malloc(sizeof(lenv));
	e->parent = NULL;
	e->count = 0;
	e->syms = NULL;
	e->vals = NULL;
	return e;
}


/* Destructor */

//lval destructor
void lval_del(lval* v) {

	switch(v->type) {
		case LVAL_NUM: break;
		case LVAL_FUN: break;
		case LVAL_ERR: free(v->err); break;
		case LVAL_SYM: free(v->sym); break;
		case LVAL_QEXPR:
		case LVAL_SEXPR:
			for (int i = 0; i < v->count; i++)
				lval_del(v->cell[i]);
			free(v->cell);
			break;
		case LVAL_FUN:
			if(!v->builtin) {
				lenv_del(v->env);
				lval_del(v->formals);
				lval_del(v->body);
			}
	}

	free(v);
}

// env destructor
void lenv_del(lenv* e) {
	for(int i = 0; i < e->count; i++) {
		free(e->syms[i]);
		lval_del(e->vals[i]);
	}
	free(e->syms);
	free(e->vals);
	free(e);
}


/* Utilities */

lval* lval_copy(lval* v) {
	lval* x = malloc(sizeof(lval));
	x->type = v->type;

	switch(v->type) {
		case LVAL_FUN: x->fun = v->fun; break;
		case LVAL_NUM: x->num = v->num; break;
		case LVAL_ERR:
			x->err = malloc(strlen(v->err)+1);
			strcpy(x->err, v->err);
			break;
		case LVAL_SYM:
			x->sym = malloc(strlen(v->sym)+1);
			strcpy(x->sym, v->sym);
			break;
		case LVAL_SEXPR:
		case LVAL_QEXPR:
			x->count = v->count;
			x->cell = malloc(sizeof(lval*) * x->count);
			for(int i = 0; i < x->count; i++)
				x->cell[i] = lval_copy(v->cell[i]);
			break;
		case LVAL_FUN:
			if(v->builtin) {
				x->builtin = v->builtin;
			} else {
				x->builtin = NULL;
				x->env = lenv_copy(v->env);
				x->formals = lval_copy(v->formals);
				x->body = lval_copy(v->body);
			}
			break;
	}
	
	return x;
}

lval* lval_add(lval* v, lval* x) {
	v->count++;
	v->cell = realloc(v->cell, sizeof(lval*) * v->count);
	v->cell[v->count-1] = x;
	return v;
}

lval* lval_join(lval* x, lval* y) {
	while(y->count) {
		x = lval_add(x, lval_pop(y, 0));
	}

	lval_del(y);
	return x;
}

lval* lval_pop(lval* v, int i) {
	lval* x = v->cell[i];
	memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));
	v->count--;
	v->cell = realloc(v->cell, sizeof(lval*) * v->count);
	return x;
}

lval* lval_take(lval* v, int i) {
	lval* x = lval_pop(v, i);
	lval_del(v);
	return x;
}


/* Print */

void lval_print(lval* v) {
	switch (v->type) {
		case LVAL_NUM: printf("%li", v->num); break;
		case LVAL_FUN: 
			if(v->builtin)
				printf("<builtin>");
			else {
				printf("(\\ ");
				lval_print(v->formals);
				putchar(' ');
				lval_print(v->body);
				putchar(')');
			}
			break;
		case LVAL_ERR: printf("Error: %s", v->err); break;
		case LVAL_SYM: printf("%s", v->sym); break;
		case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
		case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
	}
}


void lval_expr_print(lval* v, char open, char close) {
	putchar(open);
	for (int i = 0; i < v->count; i++) {
		lval_print(v->cell[i]);

		if(i != (v->count - 1))
			putchar(' ');
	}
	putchar(close);
}

void lval_println(lval* v) { lval_print(v); printf("\n"); }

/* Lisp Environment */

lval* lenv_get(lenv* e, lval* k) {
	for (int i = 0; i < e->count; i++)
		if (strcmp(e->syms[i], k->sym) == 0)
			return lval_copy(e->vals[i]);
	return lval_err("Unbound symbol '%s'", k->sym);
}

void lenv_put(lenv* e, lval* k, lval* v) {
	for (int i = 0; i < e->count; i++) {
		if(strcmp(e->syms[i], k->sym) == 0) {
			lval_del(e->vals[i]);
			e->vals[i] = lval_copy(v);
			return;
		}
	}

	e->count++;
	e->vals = realloc(e->vals, sizeof(lval*) * e->count);
	e->syms = realloc(e->syms, sizeof(lval*) * e->count);

	e->vals[e->count-1] = lval_copy(v);
	e->syms[e->count-1] = malloc(strlen(k->sym)+1);
	strcpy(e->syms[e->count-1], k->sym);
}

/* Builtin */

lval* builtin_def(lenv* e, lval* a) {
	LASSERT_TYPE("def", a, 0, LVAL_QEXPR);
	
	// First argument is a symbol list
	lval* syms = a->cell[0];
	
	// Ensure first list only contain elements that are symbols
	for (int i = 0; i < syms->count; i++) {
		LASSERT(a, syms->cell[i]->type == LVAL_SYM,
			"Function 'def' cannot define non-symbols! "
			"Got %s, expected %s.",
			ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
	}
	
	// Check correct number of symbols and values
	LASSERT(a, (syms->count == a->count-1),
		"Function 'def' passed too many arguments for symbols. "
		"Got %i, expected %i",
		syms->count, a->count-1);
	
	for (int i = 0; i < syms->count; i++)
		lenv_put(e, syms->cell[i], a->cell[i+1]);

	lval_del(a);
	return lval_sexpr();
}

lval* builtin_list(lenv* e, lval* a) {
	a->type = LVAL_QEXPR;
	return a;
}

lval* builtin_head(lenv* e, lval* a) {
	LASSERT_NUM("head", a, 1);
	LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
	LASSERT_NOT_EMPTY("head", a, 0);

	lval* v = lval_take(a, 0);
	while(v->count > 1) {lval_del(lval_pop(v, 1));}
	return v;
}

lval* builtin_tail(lenv* e, lval* a) {
	LASSERT_NUM("tail", a, 1);
	LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
	LASSERT_NOT_EMPTY("tail", a, 0);

	lval* v = lval_take(a, 0);
	lval_del(lval_pop(v, 0));
	return v;
}

lval* builtin_eval(lenv* e, lval* a) {
	LASSERT_NUM("eval", a, 1);
	LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);

	lval* x = lval_take(a, 0);
	x->type = LVAL_SEXPR;
	return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval* a) {
	for(int i = 0; i < a->count; i++) {
		LASSERT_TYPE("join", a, i, LVAL_QEXPR);
	}

	lval* x = lval_pop(a, 0);

	while(a->count) {
		x = lval_join(x, lval_pop(a, 0));
	}

	lval_del(a);
	return x;
}

lval* builtin_op(lenv* e, lval* a, char* op) {

	for (int i = 0; i < a->count; i++) {
		LASSERT_TYPE(op, a, i, LVAL_NUM);
	}

	lval* x = lval_pop(a, 0);

	if ((strcmp(op, "-") == 0) && a->count == 0)
		x->num *= -1; // = -x->num;

	while(a->count > 0) {

		lval* y = lval_pop(a, 0);

		if (strcmp(op, "+") == 0) { x->num += y->num;}
		if (strcmp(op, "-") == 0) { x->num -= y->num;}
		if (strcmp(op, "*") == 0) { x->num *= y->num;}
		if (strcmp(op, "/") == 0) {
			if(y->num == 0) {
				lval_del(x);
				lval_del(y);
				x = lval_err("Division by Zero!");
				break;
			}
			x->num /= y->num;
		}

		lval_del(y);
	}
	
	lval_del(a);
	return x;
}

lval* builtin_add(lenv* e, lval* a) {
	return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
	return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
	return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
	return builtin_op(e, a, "/");
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
	lval* k = lval_sym(name);
	lval* v = lval_fun(func);
	lenv_put(e, k, v);
	lval_del(k);
	lval_del(v);
}

void lenv_add_builtins(lenv* e) {
	/* Variable functions */
	lenv_add_builtin(e, "def", builtin_def);
	lenv_add_builtin(e, "\\", builtin_lambda)

	/* list functions */
	lenv_add_builtin(e, "list", builtin_list);
	lenv_add_builtin(e, "head", builtin_head);
	lenv_add_builtin(e, "tail", builtin_tail);
	lenv_add_builtin(e, "eval", builtin_eval);
	lenv_add_builtin(e, "join", builtin_join);
	
	/*math functions */
	lenv_add_builtin(e, "+", builtin_add);
	lenv_add_builtin(e, "-", builtin_sub);
	lenv_add_builtin(e, "*", builtin_mul);
	lenv_add_builtin(e, "/", builtin_div);
}

/* Evaluation */

lval* lval_eval_sexpr(lenv* e, lval* v) {

	for(int i = 0; i < v->count; i++) {
		v->cell[i] = lval_eval(e, v->cell[i]);
	}

	for(int i = 0; i < v->count; i++) {
		if(v->cell[i]->type == LVAL_ERR) return lval_take(v, i);
	}

	if (v->count == 0) return v;
	if (v->count == 1) return lval_take(v, 0);

	lval* f = lval_pop(v, 0);

	if (f->type != LVAL_FUN) {
		lval* err = lval_err(
			"S-Expression starts with incorrect type. "
			"Got %s, expected %s",
			ltype_name(f->type), ltype_name(LVAL_FUN));
		lval_del(f);
		lval_del(v);
		return err;
	}

	lval* result = f->fun(e, v);
	lval_del(f);
	return result;
}

lval* lval_eval(lenv* e, lval* v) {
	if(v->type == LVAL_SYM) {
		lval* x = lenv_get(e, v);
		lval_del(v);
		return x;
	}	

	if(v->type == LVAL_SEXPR) return lval_eval_sexpr(e, v);
	
	return v;
}

/* Reading */

lval* lval_read_num(mpc_ast_t* t) {
	errno = 0;
	long x = strtol(t->contents, NULL, 10);
	return errno != ERANGE ? lval_num(x) : lval_err("Invalid Number");
}

lval* lval_read(mpc_ast_t* t) {

	if(strstr(t->tag, "number")) return lval_read_num(t);
	if(strstr(t->tag, "symbol")) return lval_sym(t->contents);

	// in case of root (<), sexpr, or qexpr create empty list
	lval* x = NULL;
	// same as sample code, but should this be a series of if/else?
	if(strcmp(t->tag, ">") == 0) x = lval_sexpr();
	if(strstr(t->tag, "sexpr" )) x = lval_sexpr();
	if(strstr(t->tag, "qexpr" )) x = lval_qexpr();

	for (int i = 0; i < t->children_num; i++)
	{
		if (strcmp(t->children[i]->contents, "(") == 0) continue;
		if (strcmp(t->children[i]->contents, ")") == 0) continue;
		if (strcmp(t->children[i]->contents, "}") == 0) continue;
		if (strcmp(t->children[i]->contents, "{") == 0) continue;
		//"regex" tag does not seem to have been talked about earlier
		if (strcmp(t->children[i]->tag, "regex") == 0) continue;

		x = lval_add(x, lval_read(t->children[i]));
	}

	return x;
}

/* main */
int main (int argc, char **argv) {

	/* MPC parsers */
	mpc_parser_t* Number = mpc_new("number");
	mpc_parser_t* Symbol = mpc_new("symbol");
	mpc_parser_t* Sexpr = mpc_new("sexpr");
	mpc_parser_t* Qexpr = mpc_new("qexpr");
	mpc_parser_t* Expr = mpc_new("expr");
	mpc_parser_t* Lipl = mpc_new("lipl");

	/* MPC Grammar */
	mpca_lang(MPCA_LANG_DEFAULT,
	  " number	: /-?[0-9]+/ ;								\
	    symbol	: /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;		\
	    sexpr	: '(' <expr>* ')' ;							\
	    qexpr	: '{' <expr>* '}' ;							\
	    expr	: <number> | <symbol> | <sexpr> | <qexpr> ;	\
	    lipl	: /^/ <expr>* /$/ ;" ,
	  Number, Symbol, Sexpr, Qexpr, Expr, Lipl);

	puts("lipl version 0.0.0.0.7");
	puts("Press ctrl+c to exit");
	//puts("");

	/* build environment */
	lenv* e = lenv_new();
	lenv_add_builtins(e);

	while(1) {
		char* input = readline("lipl>>> ");
		add_history(input);

		mpc_result_t r;
		if (mpc_parse("<stdin>", input, Lipl, &r)) {
			lval* x = lval_eval(e, lval_read(r.output));
			/*lval* x = lval_read(r.output);*/
			lval_println(x);
			lval_del(x);
			mpc_ast_delete(r.output);
		} else {
			mpc_err_print(r.error);
			mpc_err_delete(r.error);
		}

		free(input);
	}

	/* Clean Up */
	lenv_del(e);
	mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lipl);

	/* Exit Successfully */	
	return 0;
}