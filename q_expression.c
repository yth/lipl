// #include <stdio.h>
// #include <stdlib.h>

#include <editline/readline.h>
#include <editline/history.h>

#include "mpc.h"

#define LASSERT(args, cond, err) \
	if (!(cond)) {lval_del(args); return lval_err(err); }

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

// new lval type
typedef struct lval{
	int type;
	long num;
	char* err;
	char* sym;
	int count;
	struct lval** cell;
} lval;

// lval constructor for numbers
lval* lval_num(long x) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_NUM;
	v->num = x;
	return v;
}

//lval constructor for errors
lval* lval_err(char* error_message) {
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_ERR;
	v->err = malloc(strlen(error_message) + 1);
	strcpy(v->err, error_message);
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

//lval destructor
void lval_del(lval* v) {

	switch(v->type) {
		case LVAL_NUM: break;
		case LVAL_ERR: free(v->err); break;
		case LVAL_SYM: free(v->sym); break;
		case LVAL_QEXPR: 
		case LVAL_SEXPR: 
			for (int i = 0; i < v->count; i++)
				lval_del(v->cell[i]);
			free(v->cell);
			break;
	}

	free(v);
}

// read mpc_ast_t with number contents into lval
lval* lval_read_num(mpc_ast_t* t) {
	errno = 0;
	long x = strtol(t->contents, NULL, 10);
	return errno != ERANGE ? lval_num(x) : lval_err("Invalid Number");
}

// general read mpc_ast_t into lval
lval* lval_add(lval* v, lval* x) {
	v->count++;
	v->cell = realloc(v->cell, sizeof(lval*) * v->count);
	v->cell[v->count-1] = x;
	return v;
}

lval* lval_read(mpc_ast_t* t) {

	if(strstr(t->tag, "number")) return lval_read_num(t);
	if(strstr(t->tag, "symbol")) return lval_sym(t->contents);

	// in case of root (<), sexpr, or qexpr create empty list
	lval* x = NULL;
	//if(strcmp(t->tag, ">") == 0 || strstr(t->tag, "sexpr"))
	if(strcmp(t->tag, ">") == 0) {x = lval_sexpr();}
        if(strstr(t->tag, "sexpr")) {x = lval_sexpr();}
	if(strstr(t->tag, "qexpr")) {x = lval_qexpr();}

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

// printing
void lval_expr_print(lval* v, char open, char close);

void lval_print(lval* v) {
	switch (v->type) {
		case LVAL_NUM: printf("%li", v->num); break;
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

		if(i != (v->count -1))
			putchar(' ');
	}
	putchar(close);
}

void lval_println(lval* v) { lval_print(v); printf("\n"); }

// Evaluation
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

lval* builtin_op(lval* a, char* op) {

	for (int i = 0; i < a->count; i++) {
		if (a->cell[i]->type != LVAL_NUM) {
			lval_del(a);
			return lval_err("Cannot operate on a non-number.");
		}
	}

	lval* x = lval_pop(a, 0);

	if ((strcmp(op, "-") == 0) && a->count == 0)
		x->num = -x->num; //*= -1? test after the whole interpreter works

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

lval* lval_eval(lval* v);

lval* lval_eval_sexpr(lval* v) {

	for(int i = 0; i < v->count; i++) {
		v->cell[i] = lval_eval(v->cell[i]);
	}

	for(int i = 0; i < v->count; i++) {
		if(v->cell[i]->type == LVAL_ERR) return lval_take(v, i);
	}

	if (v->count == 0) return v;
	if (v->count == 1) return lval_take(v, 0);

	lval* f = lval_pop(v, 0);

	if (f->type != LVAL_SYM) {
		lval_del(f);
		lval_del(v);
		return lval_err("S-Expression does not start with a symbol!");
	}

	lval* result = builtin_op(v, f->sym);
	lval_del(f);
	return result;
}

lval* lval_eval(lval* v) {
	if(v->type == LVAL_SEXPR) return lval_eval_sexpr(v);

	return v;
}

//main
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
	  " number	: /-?[0-9]+/ ;					\
	    symbol	: \"list\" | \"head\" | \"tail\" | \"join\" 	\
	    		| \"eval\" | '+' | '-' | '*' | '/' ;		\
	    sexpr	: '(' <expr>* ')' ;				\
	    qexpr	: '{' <expr>* '}' ;				\
	    expr	: <number> | <symbol> | <sexpr> | <qexpr> ;	\
	    lipl	: /^/ <expr>* /$/ ;" ,
	  Number, Symbol, Sexpr, Qexpr, Expr, Lipl);

	puts("lipl version 0.0.0.0.5");
	puts("Press ctrl + c to exit");
	puts("");

	while(1) {
		char* input = readline("lipl>>> ");
		add_history(input);
		
		mpc_result_t r;
		if (mpc_parse("<stdin>", input, Lipl, &r)) { 
			// lval* x = lval_eval(lval_read(r.output));
			lval* x = lval_read(r.output);
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
	mpc_cleanup(5, Number, Symbol, Sexpr, Qexpr, Expr, Lipl);
}