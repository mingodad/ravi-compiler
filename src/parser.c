/*
A parser and syntax tree builder for Ravi. This is work in progress.
Once ready it will be used to create a new byte code generator for Ravi.

The parser will perform following actions:

a) Generate syntax tree
b) Perform type checking (Ravi enhancement)

Copyright (C) 2018-2020 Dibyendu Majumdar

Note that the overall structure of the parser is loosely based on the Lua 5.3 parser.
*/

#include "fnv_hash.h"
#include <ravi_ast.h>

/* forward declarations */
static struct ast_node *parse_expression(struct parser_state *);
static void parse_statement_list(struct parser_state *, struct ast_node_list **list);
static struct ast_node *parse_statement(struct parser_state *);
static struct ast_node *new_function(struct parser_state *parser);
static struct ast_node *end_function(struct parser_state *parser);
static struct block_scope *new_scope(struct parser_state *parser);
static void end_scope(struct parser_state *parser);
static struct ast_node *new_literal_expression(struct parser_state *parser, ravitype_t type);
static struct ast_node *generate_label(struct parser_state *parser, const struct string_object *label);
static void add_local_symbol_to_current_scope(struct parser_state *parser, struct lua_symbol *sym);

static void add_symbol(struct compiler_state *container, struct lua_symbol_list **list, struct lua_symbol *sym)
{
	ptrlist_add((struct ptr_list **)list, sym, &container->ptrlist_allocator);
}

static void add_ast_node(struct compiler_state *container, struct ast_node_list **list, struct ast_node *node)
{
	ptrlist_add((struct ptr_list **)list, node, &container->ptrlist_allocator);
}

static struct ast_node *allocate_ast_node(struct parser_state *parser, enum ast_node_type type) {
	struct ast_node *node = (struct ast_node *)raviX_allocator_allocate(&parser->container->ast_node_allocator, 0);
	node->type = type;
	node->line_number = parser->ls->lastline;
	return node;
}

static void error_expected(struct lexer_state *ls, int token)
{
	raviX_token2str(token, &ls->container->error_message);
	raviX_buffer_add_string(&ls->container->error_message, " expected");
	longjmp(ls->container->env, 1);
}

static int testnext(struct lexer_state *ls, int c)
{
	if (ls->t.token == c) {
		raviX_next(ls);
		return 1;
	} else
		return 0;
}

static void check(struct lexer_state *ls, int c)
{
	if (ls->t.token != c)
		error_expected(ls, c);
}

static void checknext(struct lexer_state *ls, int c)
{
	check(ls, c);
	raviX_next(ls);
}

/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/

/*
** check whether current token is in the follow set of a block.
** 'until' closes syntactical blocks, but do not close scope,
** so it is handled in separate.
*/
static int block_follow(struct lexer_state *ls, int withuntil)
{
	switch (ls->t.token) {
	case TOK_ELSE:
	case TOK_ELSEIF:
	case TOK_END:
	case TOK_EOS:
		return 1;
	case TOK_UNTIL:
		return withuntil;
	default:
		return 0;
	}
}

static void check_match(struct lexer_state *ls, int what, int who, int where)
{
	if (!testnext(ls, what)) {
		if (where == ls->linenumber)
			error_expected(ls, what);
		else {
			membuff_t mb;
			raviX_buffer_init(&mb, 256);
			raviX_token2str(what, &mb);
			raviX_buffer_add_string(&mb, " expected (to close ");
			raviX_token2str(who, &mb);
			raviX_buffer_add_fstring(&mb, " at line %d)", where);
			char message[1024];
			raviX_string_copy(message, raviX_buffer_data(&mb), sizeof message);
			raviX_buffer_free(&mb);
			raviX_syntaxerror(ls, message);
		}
	}
}

/* Check that current token is a name, and advance */
static const struct string_object *check_name_and_next(struct lexer_state *ls)
{
	const struct string_object *ts;
	check(ls, TOK_NAME);
	ts = ls->t.seminfo.ts;
	raviX_next(ls);
	return ts;
}

/* create a new local variable in function scope, and set the
 * variable type (RAVI - added type tt) */
static struct lua_symbol *new_local_symbol(struct parser_state *parser, const struct string_object *name, ravitype_t tt,
					   const struct string_object *usertype)
{
	struct block_scope *scope = parser->current_scope;
	struct lua_symbol *symbol = raviX_allocator_allocate(&parser->container->symbol_allocator, 0);
	set_typename(&symbol->variable.value_type, tt, usertype);
	symbol->symbol_type = SYM_LOCAL;
	symbol->variable.block = scope;
	symbol->variable.var_name = name;
	symbol->variable.pseudo = NULL;
	return symbol;
}

/* create a new label */
static struct lua_symbol *new_label(struct parser_state *parser, const struct string_object *name)
{
	struct block_scope *scope = parser->current_scope;
	assert(scope);
	struct lua_symbol *symbol = raviX_allocator_allocate(&parser->container->symbol_allocator, 0);
	symbol->symbol_type = SYM_LABEL;
	symbol->label.block = scope;
	symbol->label.label_name = name;
	// Add to the end of the symbol list
	// Note that Lua allows multiple local declarations of the same name
	// so a new instance just gets added to the end
	add_symbol(parser->container, &scope->symbol_list, symbol);
	return symbol;
}

/* create a new local variable
 */
static struct lua_symbol *new_localvarliteral_(struct parser_state *parser, const char *name, size_t sz)
{
	return new_local_symbol(parser, raviX_create_string(parser->container, name, (uint32_t)sz), RAVI_TANY, NULL);
}

/* create a new local variable
 */
#define new_localvarliteral(parser, name) new_localvarliteral_(parser, "" name, (sizeof(name) / sizeof(char)) - 1)

static struct lua_symbol *search_for_variable_in_block(struct block_scope *scope, const struct string_object *varname)
{
	struct lua_symbol *symbol;
	// Lookup in reverse order so that we discover the
	// most recently added local symbol - as Lua allows same
	// symbol to be declared local more than once in a scope
	// Should also work with nesting as the function when parsed
	// will only know about vars declared in parent function until
	// now.
	FOR_EACH_PTR_REVERSE(scope->symbol_list, symbol)
	{
		switch (symbol->symbol_type) {
		case SYM_LOCAL: {
			if (varname == symbol->variable.var_name) {
				return symbol;
			}
			break;
		}
		default:
			break;
		}
	}
	END_FOR_EACH_PTR_REVERSE(symbol);
	return NULL;
}

/* Each function has a list of upvalues, searches this list for given name
 */
static struct lua_symbol *search_upvalue_in_function(struct ast_node *function, const struct string_object *name)
{
	struct lua_symbol *symbol;
	FOR_EACH_PTR(function->function_expr.upvalues, symbol)
	{
		switch (symbol->symbol_type) {
		case SYM_UPVALUE: {
			assert(symbol->upvalue.target_variable->symbol_type == SYM_LOCAL);
			if (name == symbol->upvalue.target_variable->variable.var_name) {
				return symbol;
			}
			break;
		}
		default:
			break;
		}
	}
	END_FOR_EACH_PTR(symbol);
	return NULL;
}

/* Each function has a list of upvalues, searches this list for given name, and adds it if not found.
 * Returns true if added, false means the function already has the upvalue.
 */
static bool add_upvalue_in_function(struct parser_state *parser, struct ast_node *function, struct lua_symbol *sym)
{
	assert(sym->symbol_type == SYM_LOCAL);
	struct lua_symbol *symbol;
	FOR_EACH_PTR(function->function_expr.upvalues, symbol)
	{
		switch (symbol->symbol_type) {
		case SYM_UPVALUE: {
			assert(symbol->upvalue.target_variable->symbol_type == SYM_LOCAL);
			if (sym == symbol->upvalue.target_variable) {
				return false;
			}
			break;
		}
		default:
			break;
		}
	}
	END_FOR_EACH_PTR(symbol);
	struct lua_symbol *upvalue = raviX_allocator_allocate(&parser->container->symbol_allocator, 0);
	upvalue->symbol_type = SYM_UPVALUE;
	upvalue->upvalue.target_variable = sym;
	upvalue->upvalue.target_function = function;
	upvalue->upvalue.upvalue_index = ptrlist_size(
	    (const struct ptr_list *)function->function_expr.upvalues); /* position of upvalue in function */
	copy_type(&upvalue->upvalue.value_type, &sym->variable.value_type);
	add_symbol(parser->container, &function->function_expr.upvalues, upvalue);
	return true;
}

/* Searches for a variable starting from current scope, going up the
 * scope chain within the current function. If the variable is not found in any scope of the function, then
 * search the function's upvalue list. Repeat the exercise in parent function until either
 * the symbol is found or we exhaust the search. NULL is returned if search was
 * exhausted.
 */
static struct lua_symbol *search_for_variable(struct parser_state *parser, const struct string_object *varname,
					      bool *is_local)
{
	*is_local = false;
	struct block_scope *current_scope = parser->current_scope;
	struct ast_node *start_function = parser->current_function;
	assert(current_scope && current_scope->function == parser->current_function);
	while (current_scope) {
		struct ast_node *current_function = current_scope->function;
		while (current_scope && current_function == current_scope->function) {
			struct lua_symbol *symbol = search_for_variable_in_block(current_scope, varname);
			if (symbol) {
				*is_local = (current_function == start_function);
				return symbol;
			}
			current_scope = current_scope->parent;
		}
		// search upvalues in the function
		struct lua_symbol *symbol = search_upvalue_in_function(current_function, varname);
		if (symbol)
			return symbol;
		// try in parent function
	}
	return NULL;
}

/* Adds an upvalue to current_function and its parents until var_function; var_function being where the symbol
 * exists as a local or an upvalue. If the symbol is found in a function's upvalue list then there is no need to
 * check parent functions.
 */
static void add_upvalue_in_levels_upto(struct parser_state *parser, struct ast_node *current_function,
				       struct ast_node *var_function, struct lua_symbol *symbol)
{
	assert(current_function != var_function);
	while (current_function && current_function != var_function) {
		bool added = add_upvalue_in_function(parser, current_function, symbol);
		if (!added)
			// this function already has it so we are done
			break;
		current_function = current_function->function_expr.parent_function;
	}
}

/* Creates a symbol reference to the name; the returned symbol reference
 * may be local, upvalue or global.
 */
static struct ast_node *new_symbol_reference(struct parser_state *parser)
{
	const struct string_object *varname = check_name_and_next(parser->ls);
	bool is_local = false;
	struct lua_symbol *symbol = search_for_variable(parser, varname, &is_local);
	if (symbol) {
		// TODO we had a bug here - see t013.lua
		// Need more test cases for this
		// we found a local or upvalue
		if (!is_local && symbol->symbol_type == SYM_LOCAL) {
			// If the local symbol occurred in a parent function then we
			// need to construct an upvalue. Lua requires that the upvalue be
			// added to all functions in the tree up to the function where the local
			// is defined.
			add_upvalue_in_levels_upto(parser, parser->current_function, symbol->variable.block->function,
						   symbol);
			// TODO Following search could be avoided if above returned the symbol
			symbol = search_upvalue_in_function(parser->current_function, varname);
		} else if (symbol->symbol_type == SYM_UPVALUE && symbol->upvalue.target_function != parser->current_function) {
			// We found an upvalue but it is not at the same level
			// Ensure all levels have the upvalue
			add_upvalue_in_levels_upto(parser, parser->current_function, symbol->upvalue.target_function,
						   symbol->upvalue.target_variable);
			// TODO Following search could be avoided if above returned the symbol
			symbol = search_upvalue_in_function(parser->current_function, varname);
		}
	} else {
		// Return global symbol
		struct lua_symbol *global = raviX_allocator_allocate(&parser->container->symbol_allocator, 0);
		global->symbol_type = SYM_GLOBAL;
		global->variable.var_name = varname;
		global->variable.block = NULL;
		set_type(&global->variable.value_type, RAVI_TANY); // Globals are always ANY type
		// We don't add globals to any scope so that they are
		// always looked up
		symbol = global;
	}
	struct ast_node *symbol_expr = allocate_ast_node(parser, EXPR_SYMBOL);
	symbol_expr->symbol_expr.type = symbol->variable.value_type;
	symbol_expr->symbol_expr.var = symbol;
	return symbol_expr;
}

/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/

static struct ast_node *new_string_literal(struct parser_state *parser, const struct string_object *ts)
{
	struct ast_node *node = allocate_ast_node(parser, EXPR_LITERAL);
	set_type(&node->literal_expr.type, RAVI_TSTRING);
	node->literal_expr.u.ts = ts;
	return node;
}

static struct ast_node *new_field_selector(struct parser_state *parser, const struct string_object *ts)
{
	struct ast_node *index = allocate_ast_node(parser, EXPR_FIELD_SELECTOR);
	index->index_expr.expr = new_string_literal(parser, ts);
	set_type(&index->index_expr.type, RAVI_TANY);
	return index;
}

/*
 * Parse ['.' | ':'] NAME
 */
static struct ast_node *parse_field_selector(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* fieldsel -> ['.' | ':'] NAME */
	raviX_next(ls); /* skip the dot or colon */
	const struct string_object *ts = check_name_and_next(ls);
	return new_field_selector(parser, ts);
}

/*
 * Parse '[' expr ']
 */
static struct ast_node *parse_yindex(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* index -> '[' expr ']' */
	raviX_next(ls); /* skip the '[' */
	struct ast_node *expr = parse_expression(parser);
	checknext(ls, ']');

	struct ast_node *index = allocate_ast_node(parser, EXPR_Y_INDEX);
	index->index_expr.expr = expr;
	set_type(&index->index_expr.type, RAVI_TANY);
	return index;
}

/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/

static struct ast_node *new_indexed_assign_expr(struct parser_state *parser, struct ast_node *key_expr,
						struct ast_node *value_expr)
{
	struct ast_node *set = allocate_ast_node(parser, EXPR_TABLE_ELEMENT_ASSIGN);
	set->table_elem_assign_expr.key_expr = key_expr;
	set->table_elem_assign_expr.value_expr = value_expr;
	set->table_elem_assign_expr.type =
	    value_expr->common_expr.type; /* type of indexed assignment is same as the value*/
	return set;
}

static struct ast_node *parse_recfield(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* recfield -> (NAME | '['exp1']') = exp1 */
	struct ast_node *index_expr;
	if (ls->t.token == TOK_NAME) {
		const struct string_object *ts = check_name_and_next(ls);
		index_expr = new_field_selector(parser, ts);
	} else /* ls->t.token == '[' */
		index_expr = parse_yindex(parser);
	checknext(ls, '=');
	struct ast_node *value_expr = parse_expression(parser);
	return new_indexed_assign_expr(parser, index_expr, value_expr);
}

static struct ast_node *parse_listfield(struct parser_state *parser)
{
	/* listfield -> exp */
	struct ast_node *value_expr = parse_expression(parser);
	return new_indexed_assign_expr(parser, NULL, value_expr);
}

static struct ast_node *parse_field(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* field -> listfield | recfield */
	switch (ls->t.token) {
	case TOK_NAME: {				/* may be 'listfield' or 'recfield' */
		if (raviX_lookahead(ls) != '=') /* expression? */
			return parse_listfield(parser);
		else
			return parse_recfield(parser);
		break;
	}
	case '[': {
		return parse_recfield(parser);
		break;
	}
	default: {
		return parse_listfield(parser);
		break;
	}
	}
	return NULL;
}

static struct ast_node *parse_table_constructor(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* constructor -> '{' [ field { sep field } [sep] ] '}'
	sep -> ',' | ';' */
	int line = ls->linenumber;
	checknext(ls, '{');
	struct ast_node *table_expr = allocate_ast_node(parser, EXPR_TABLE_LITERAL);
	set_type(&table_expr->table_expr.type, RAVI_TTABLE);
	table_expr->table_expr.expr_list = NULL;
	do {
		if (ls->t.token == '}')
			break;
		struct ast_node *field_expr = parse_field(parser);
		add_ast_node(parser->container, &table_expr->table_expr.expr_list, field_expr);
	} while (testnext(ls, ',') || testnext(ls, ';'));
	check_match(ls, '}', '{', line);
	return table_expr;
}

/* }====================================================================== */

/*
 * We would like to allow user defined types to contain the sequence
 * NAME [. NAME]+
 * The initial NAME is supplied.
 * Returns extended name.
 * Note that the returned string will be anchored in the Lexer and must
 * be anchored somewhere else by the time parsing finishes
 */
static const struct string_object *parse_user_defined_type_name(struct lexer_state *ls,
								const struct string_object *typename)
{
	size_t len = 0;
	if (testnext(ls, '.')) {
		char buffer[256] = {0};
		const char *str = typename->str;
		len = strlen(str);
		if (len >= sizeof buffer) {
			raviX_syntaxerror(ls, "User defined type name is too long");
			return typename;
		}
		snprintf(buffer, sizeof buffer, "%s", str);
		do {
			typename = check_name_and_next(ls);
			str = typename->str;
			size_t newlen = len + strlen(str) + 1;
			if (newlen >= sizeof buffer) {
				raviX_syntaxerror(ls, "User defined type name is too long");
				return typename;
			}
			snprintf(buffer + len, sizeof buffer - len, ".%s", str);
			len = newlen;
		} while (testnext(ls, '.'));
		typename = raviX_create_string(ls->container, buffer, (uint32_t)strlen(buffer));
	}
	return typename;
}

/* RAVI Parse
 *   name : type
 *   where type is 'integer', 'integer[]',
 *                 'number', 'number[]'
 */
static struct lua_symbol *parse_local_variable_declaration(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* assume a dynamic type */
	ravitype_t tt = RAVI_TANY;
	const struct string_object *name = check_name_and_next(ls);
	const struct string_object *pusertype = NULL;
	if (testnext(ls, ':')) {
		const struct string_object *typename = check_name_and_next(ls); /* we expect a type name */
		const char *str = typename->str;
		/* following is not very nice but easy as
		 * the lexer doesn't need to be changed
		 */
		if (strcmp(str, "integer") == 0)
			tt = RAVI_TNUMINT;
		else if (strcmp(str, "number") == 0)
			tt = RAVI_TNUMFLT;
		else if (strcmp(str, "closure") == 0)
			tt = RAVI_TFUNCTION;
		else if (strcmp(str, "table") == 0)
			tt = RAVI_TTABLE;
		else if (strcmp(str, "string") == 0)
			tt = RAVI_TSTRING;
		else if (strcmp(str, "boolean") == 0)
			tt = RAVI_TBOOLEAN;
		else if (strcmp(str, "any") == 0)
			tt = RAVI_TANY;
		else {
			/* default is a userdata type */
			tt = RAVI_TUSERDATA;
			typename = parse_user_defined_type_name(ls, typename);
			// str = getstr(typename);
			pusertype = typename;
		}
		if (tt == RAVI_TNUMFLT || tt == RAVI_TNUMINT) {
			/* if we see [] then it is an array type */
			if (testnext(ls, '[')) {
				checknext(ls, ']');
				tt = (tt == RAVI_TNUMFLT) ? RAVI_TARRAYFLT : RAVI_TARRAYINT;
			}
		}
	}
	return new_local_symbol(parser, name, tt, pusertype);
}

static bool parse_parameter_list(struct parser_state *parser, struct lua_symbol_list **list)
{
	struct lexer_state *ls = parser->ls;
	/* parlist -> [ param { ',' param } ] */
	int nparams = 0;
	bool is_vararg = false;
	if (ls->t.token != ')') { /* is 'parlist' not empty? */
		do {
			switch (ls->t.token) {
			case TOK_NAME: { /* param -> NAME */
					/* RAVI change - add type */
				struct lua_symbol *symbol = parse_local_variable_declaration(parser);
				add_symbol(parser->container, list, symbol);
				add_local_symbol_to_current_scope(parser, symbol);
				nparams++;
				break;
			}
			case TOK_DOTS: { /* param -> '...' */
				raviX_next(ls);
				is_vararg = true; /* declared vararg */
				break;
			}
			default:
				raviX_syntaxerror(ls, "<name> or '...' expected");
			}
		} while (!is_vararg && testnext(ls, ','));
	}
	return is_vararg;
}

static void parse_function_body(struct parser_state *parser, struct ast_node *func_ast, int ismethod, int line)
{
	struct lexer_state *ls = parser->ls;
	/* body ->  '(' parlist ')' block END */
	checknext(ls, '(');
	if (ismethod) {
		struct lua_symbol *symbol = new_localvarliteral(parser, "self"); /* create 'self' parameter */
		add_symbol(parser->container, &func_ast->function_expr.args, symbol);
	}
	bool is_vararg = parse_parameter_list(parser, &func_ast->function_expr.args);
	func_ast->function_expr.is_vararg = is_vararg;
	func_ast->function_expr.is_method = ismethod;
	checknext(ls, ')');
	parse_statement_list(parser, &func_ast->function_expr.function_statement_list);
	check_match(ls, TOK_END, TOK_FUNCTION, line);
}

/* parse expression list */
static int parse_expression_list(struct parser_state *parser, struct ast_node_list **list)
{
	struct lexer_state *ls = parser->ls;
	/* explist -> expr { ',' expr } */
	int n = 1; /* at least one expression */
	struct ast_node *expr = parse_expression(parser);
	add_ast_node(parser->container, list, expr);
	while (testnext(ls, ',')) {
		expr = parse_expression(parser);
		add_ast_node(parser->container, list, expr);
		n++;
	}
	return n;
}

/* parse function arguments */
static struct ast_node *parse_function_call(struct parser_state *parser, const struct string_object *methodname,
					    int line)
{
	struct lexer_state *ls = parser->ls;
	struct ast_node *call_expr = allocate_ast_node(parser, EXPR_FUNCTION_CALL);
	call_expr->function_call_expr.method_name = methodname;
	call_expr->function_call_expr.arg_list = NULL;
	set_type(&call_expr->function_call_expr.type, RAVI_TANY);
	switch (ls->t.token) {
	case '(': { /* funcargs -> '(' [ explist ] ')' */
		raviX_next(ls);
		if (ls->t.token == ')') /* arg list is empty? */
			;
		else {
			parse_expression_list(parser, &call_expr->function_call_expr.arg_list);
		}
		check_match(ls, ')', '(', line);
		break;
	}
	case '{': { /* funcargs -> constructor */
		struct ast_node *table_expr = parse_table_constructor(parser);
		add_ast_node(parser->container, &call_expr->function_call_expr.arg_list, table_expr);
		break;
	}
	case TOK_STRING: { /* funcargs -> STRING */
		struct ast_node *string_expr = new_literal_expression(parser, RAVI_TSTRING);
		string_expr->literal_expr.u.ts = ls->t.seminfo.ts;
		add_ast_node(parser->container, &call_expr->function_call_expr.arg_list, string_expr);
		raviX_next(ls);
		break;
	}
	default: {
		raviX_syntaxerror(ls, "function arguments expected");
	}
	}
	return call_expr;
}

/*
** {======================================================================
** Expression parsing
** =======================================================================
*/

/* primary expression - name or subexpression */
static struct ast_node *parse_primary_expression(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	struct ast_node *primary_expr = NULL;
	/* primaryexp -> NAME | '(' expr ')' */
	switch (ls->t.token) {
	case '(': {
		int line = ls->linenumber;
		raviX_next(ls);
		primary_expr = parse_expression(parser);
		check_match(ls, ')', '(', line);
		break;
	}
	case TOK_NAME: {
		primary_expr = new_symbol_reference(parser);
		break;
	}
	default: {
		raviX_syntaxerror(ls, "unexpected symbol");
	}
	}
	assert(primary_expr);
	return primary_expr;
}

/* variable or field access or function call */
static struct ast_node *parse_suffixed_expression(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* suffixedexp ->
	primaryexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs } */
	int line = ls->linenumber;
	struct ast_node *suffixed_expr = allocate_ast_node(parser, EXPR_SUFFIXED);
	suffixed_expr->suffixed_expr.primary_expr = parse_primary_expression(parser);
	suffixed_expr->suffixed_expr.type = suffixed_expr->suffixed_expr.primary_expr->common_expr.type;
	suffixed_expr->suffixed_expr.suffix_list = NULL;
	for (;;) {
		switch (ls->t.token) {
		case '.': { /* fieldsel */
			struct ast_node *suffix = parse_field_selector(parser);
			add_ast_node(parser->container, &suffixed_expr->suffixed_expr.suffix_list, suffix);
			set_type(&suffixed_expr->suffixed_expr.type, RAVI_TANY);
			break;
		}
		case '[': { /* '[' exp1 ']' */
			struct ast_node *suffix = parse_yindex(parser);
			add_ast_node(parser->container, &suffixed_expr->suffixed_expr.suffix_list, suffix);
			set_type(&suffixed_expr->suffixed_expr.type, RAVI_TANY);
			break;
		}
		case ':': { /* ':' NAME funcargs */
			raviX_next(ls);
			const struct string_object *methodname = check_name_and_next(ls);
			struct ast_node *suffix = parse_function_call(parser, methodname, line);
			add_ast_node(parser->container, &suffixed_expr->suffixed_expr.suffix_list, suffix);
			break;
		}
		case '(':
		case TOK_STRING:
		case '{': { /* funcargs */
			struct ast_node *suffix = parse_function_call(parser, NULL, line);
			add_ast_node(parser->container, &suffixed_expr->suffixed_expr.suffix_list, suffix);
			break;
		}
		default:
			return suffixed_expr;
		}
	}
}

static struct ast_node *new_literal_expression(struct parser_state *parser, ravitype_t type)
{
	struct ast_node *expr = allocate_ast_node(parser, EXPR_LITERAL);
	set_type(&expr->literal_expr.type, type);
	expr->literal_expr.u.i = 0; /* initialize */
	return expr;
}

static struct ast_node *parse_simple_expression(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* simpleexp -> FLT | INT | STRING | NIL | TRUE | FALSE | ... |
	constructor | FUNCTION body | suffixedexp */
	struct ast_node *expr = NULL;
	switch (ls->t.token) {
	case TOK_FLT: {
		expr = new_literal_expression(parser, RAVI_TNUMFLT);
		expr->literal_expr.u.r = ls->t.seminfo.r;
		break;
	}
	case TOK_INT: {
		expr = new_literal_expression(parser, RAVI_TNUMINT);
		expr->literal_expr.u.i = ls->t.seminfo.i;
		break;
	}
	case TOK_STRING: {
		expr = new_literal_expression(parser, RAVI_TSTRING);
		expr->literal_expr.u.ts = ls->t.seminfo.ts;
		break;
	}
	case TOK_NIL: {
		expr = new_literal_expression(parser, RAVI_TNIL);
		expr->literal_expr.u.i = -1;
		break;
	}
	case TOK_TRUE: {
		expr = new_literal_expression(parser, RAVI_TBOOLEAN);
		expr->literal_expr.u.i = 1;
		break;
	}
	case TOK_FALSE: {
		expr = new_literal_expression(parser, RAVI_TBOOLEAN);
		expr->literal_expr.u.i = 0;
		break;
	}
	case TOK_DOTS: { /* vararg */
		// Not handled yet
		raviX_syntaxerror(parser->ls, "Var args not supported");
		expr = NULL;
		break;
	}
	case '{': { /* constructor */
		return parse_table_constructor(parser);
	}
	case TOK_FUNCTION: {
		raviX_next(ls);
		struct ast_node *function_ast = new_function(parser);
		parse_function_body(parser, function_ast, 0, ls->linenumber);
		end_function(parser);
		return function_ast;
	}
	default: {
		return parse_suffixed_expression(parser);
	}
	}
	raviX_next(ls);
	return expr;
}

static UnaryOperatorType get_unary_opr(int op)
{
	switch (op) {
	case TOK_NOT:
		return UNOPR_NOT;
	case '-':
		return UNOPR_MINUS;
	case '~':
		return UNOPR_BNOT;
	case '#':
		return UNOPR_LEN;
	case TOK_TO_INTEGER:
		return UNOPR_TO_INTEGER;
	case TOK_TO_NUMBER:
		return UNOPR_TO_NUMBER;
	case TOK_TO_INTARRAY:
		return UNOPR_TO_INTARRAY;
	case TOK_TO_NUMARRAY:
		return UNOPR_TO_NUMARRAY;
	case TOK_TO_TABLE:
		return UNOPR_TO_TABLE;
	case TOK_TO_STRING:
		return UNOPR_TO_STRING;
	case TOK_TO_CLOSURE:
		return UNOPR_TO_CLOSURE;
	case '@':
		return UNOPR_TO_TYPE;
	default:
		return UNOPR_NOUNOPR;
	}
}

static BinaryOperatorType get_binary_opr(int op)
{
	switch (op) {
	case '+':
		return BINOPR_ADD;
	case '-':
		return BINOPR_SUB;
	case '*':
		return BINOPR_MUL;
	case '%':
		return BINOPR_MOD;
	case '^':
		return BINOPR_POW;
	case '/':
		return BINOPR_DIV;
	case TOK_IDIV:
		return BINOPR_IDIV;
	case '&':
		return BINOPR_BAND;
	case '|':
		return BINOPR_BOR;
	case '~':
		return BINOPR_BXOR;
	case TOK_SHL:
		return BINOPR_SHL;
	case TOK_SHR:
		return BINOPR_SHR;
	case TOK_CONCAT:
		return BINOPR_CONCAT;
	case TOK_NE:
		return BINOPR_NE;
	case TOK_EQ:
		return BINOPR_EQ;
	case '<':
		return BINOPR_LT;
	case TOK_LE:
		return BINOPR_LE;
	case '>':
		return BINOPR_GT;
	case TOK_GE:
		return BINOPR_GE;
	case TOK_AND:
		return BINOPR_AND;
	case TOK_OR:
		return BINOPR_OR;
	default:
		return BINOPR_NOBINOPR;
	}
}

static const struct {
	lu_byte left;  /* left priority for each binary operator */
	lu_byte right; /* right priority */
} priority[] = {
    /* ORDER OPR */
    {10, 10}, {10, 10},		/* '+' '-' */
    {11, 11}, {11, 11},		/* '*' '%' */
    {14, 13},			/* '^' (right associative) */
    {11, 11}, {11, 11},		/* '/' '//' */
    {6, 6},   {4, 4},	{5, 5}, /* '&' '|' '~' */
    {7, 7},   {7, 7},		/* '<<' '>>' */
    {9, 8},			/* '..' (right associative) */
    {3, 3},   {3, 3},	{3, 3}, /* ==, <, <= */
    {3, 3},   {3, 3},	{3, 3}, /* ~=, >, >= */
    {2, 2},   {1, 1}		/* and, or */
};

#define UNARY_PRIORITY 12 /* priority for unary operators */

/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where 'binop' is any binary operator with a priority higher than 'limit'
*/
static struct ast_node *parse_sub_expression(struct parser_state *parser, int limit, BinaryOperatorType *untreated_op)
{
	struct lexer_state *ls = parser->ls;
	BinaryOperatorType op;
	UnaryOperatorType uop;
	struct ast_node *expr = NULL;
	uop = get_unary_opr(ls->t.token);
	if (uop != UNOPR_NOUNOPR) {
		// RAVI change - get usertype if @<name>
		const struct string_object *usertype = NULL;
		if (uop == UNOPR_TO_TYPE) {
			usertype = ls->t.seminfo.ts;
			raviX_next(ls);
			// Check and expand to extended name if necessary
			usertype = parse_user_defined_type_name(ls, usertype);
		} else {
			raviX_next(ls);
		}
		BinaryOperatorType ignored;
		struct ast_node *subexpr = parse_sub_expression(parser, UNARY_PRIORITY, &ignored);
		expr = allocate_ast_node(parser, EXPR_UNARY);
		expr->unary_expr.expr = subexpr;
		expr->unary_expr.unary_op = uop;
		expr->unary_expr.type.type_name = usertype;
	} else {
		expr = parse_simple_expression(parser);
	}
	/* expand while operators have priorities higher than 'limit' */
	op = get_binary_opr(ls->t.token);
	while (op != BINOPR_NOBINOPR && priority[op].left > limit) {
		BinaryOperatorType nextop;
		raviX_next(ls);
		/* read sub-expression with higher priority */
		struct ast_node *exprright = parse_sub_expression(parser, priority[op].right, &nextop);

		struct ast_node *binexpr = allocate_ast_node(parser, EXPR_BINARY);
		binexpr->binary_expr.expr_left = expr;
		binexpr->binary_expr.expr_right = exprright;
		binexpr->binary_expr.binary_op = op;
		expr = binexpr; // Becomes the left expr for next iteration
		op = nextop;
	}
	*untreated_op = op; /* return first untreated operator */
	return expr;
}

static struct ast_node *parse_expression(struct parser_state *parser)
{
	BinaryOperatorType ignored;
	return parse_sub_expression(parser, 0, &ignored);
}

/* }==================================================================== */

/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/

static void add_local_symbol_to_current_scope(struct parser_state *parser, struct lua_symbol *sym)
{
	// Note that Lua allows multiple local declarations of the same name
	// so a new instance just gets added to the end
	add_symbol(parser->container, &parser->current_scope->symbol_list, sym);
	add_symbol(parser->container, &parser->current_scope->function->function_expr.locals, sym);
}

static struct block_scope *parse_block(struct parser_state *parser, struct ast_node_list **statement_list)
{
	/* block -> statlist */
	struct block_scope *scope = new_scope(parser);
	parse_statement_list(parser, statement_list);
	end_scope(parser);
	return scope;
}

/* parse condition in a repeat statement or an if control structure
 * called by repeatstat(), test_then_block()
 */
static struct ast_node *parse_condition(struct parser_state *parser)
{
	/* cond -> exp */
	return parse_expression(parser); /* read condition */
}

static struct ast_node *parse_goto_statment(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	const struct string_object *label;
	int is_break = 0;
	if (testnext(ls, TOK_GOTO))
		label = check_name_and_next(ls);
	else {
		raviX_next(ls); /* skip break */
		label = raviX_create_string(ls->container, "break", sizeof "break");
		is_break = 1;
	}
	// Resolve labels in the end?
	struct ast_node *goto_stmt = allocate_ast_node(parser, STMT_GOTO);
	goto_stmt->goto_stmt.name = label;
	goto_stmt->goto_stmt.is_break = is_break;
	goto_stmt->goto_stmt.goto_scope = parser->current_scope;
	return goto_stmt;
}

/* skip no-op statements */
static void skip_noop_statements(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	while (ls->t.token == ';') //  || ls->t.token == TOK_DBCOLON)
		parse_statement(parser);
}

static struct ast_node *generate_label(struct parser_state *parser, const struct string_object *label)
{
	struct lua_symbol *symbol = new_label(parser, label);
	struct ast_node *label_stmt = allocate_ast_node(parser, STMT_LABEL);
	label_stmt->label_stmt.symbol = symbol;
	return label_stmt;
}

static struct ast_node *parse_label_statement(struct parser_state *parser, const struct string_object *label, int line)
{
	(void)line;
	struct lexer_state *ls = parser->ls;
	/* label -> '::' NAME '::' */
	checknext(ls, TOK_DBCOLON); /* skip double colon */
	/* create new entry for this label */
	struct ast_node *label_stmt = generate_label(parser, label);
	skip_noop_statements(parser); /* skip other no-op statements */
	return label_stmt;
}

static struct ast_node *parse_while_statement(struct parser_state *parser, int line)
{
	struct lexer_state *ls = parser->ls;
	/* whilestat -> WHILE cond DO block END */
	raviX_next(ls); /* skip WHILE */
	struct ast_node *stmt = allocate_ast_node(parser, STMT_WHILE);
	stmt->while_or_repeat_stmt.loop_scope = NULL;
	stmt->while_or_repeat_stmt.loop_statement_list = NULL;
	stmt->while_or_repeat_stmt.condition = parse_condition(parser);
	checknext(ls, TOK_DO);
	stmt->while_or_repeat_stmt.loop_scope = parse_block(parser, &stmt->while_or_repeat_stmt.loop_statement_list);
	check_match(ls, TOK_END, TOK_WHILE, line);
	return stmt;
}

static struct ast_node *parse_repeat_statement(struct parser_state *parser, int line)
{
	struct lexer_state *ls = parser->ls;
	/* repeatstat -> REPEAT block UNTIL cond */
	raviX_next(ls); /* skip REPEAT */
	struct ast_node *stmt = allocate_ast_node(parser, STMT_REPEAT);
	stmt->while_or_repeat_stmt.condition = NULL;
	stmt->while_or_repeat_stmt.loop_statement_list = NULL;
	stmt->while_or_repeat_stmt.loop_scope = new_scope(parser); /* scope block */
	parse_statement_list(parser, &stmt->while_or_repeat_stmt.loop_statement_list);
	check_match(ls, TOK_UNTIL, TOK_REPEAT, line);
	stmt->while_or_repeat_stmt.condition = parse_condition(parser); /* read condition (inside scope block) */
	end_scope(parser);
	return stmt;
}

/* parse a for loop body for both versions of the for loop */
static void parse_forbody(struct parser_state *parser, struct ast_node *stmt, int line, int nvars, int isnum)
{
	(void)line;
	(void)nvars;
	(void)isnum;
	struct lexer_state *ls = parser->ls;
	/* forbody -> DO block */
	checknext(ls, TOK_DO);
	stmt->for_stmt.for_body = parse_block(parser, &stmt->for_stmt.for_statement_list);
}

/* parse a numerical for loop */
static void parse_fornum_statement(struct parser_state *parser, struct ast_node *stmt,
				   const struct string_object *varname, int line)
{
	struct lexer_state *ls = parser->ls;
	/* fornum -> NAME = exp1,exp1[,exp1] forbody */
	struct lua_symbol *local = new_local_symbol(parser, varname, RAVI_TANY, NULL);
	add_symbol(parser->container, &stmt->for_stmt.symbols, local);
	add_local_symbol_to_current_scope(parser, local);
	checknext(ls, '=');
	/* get the type of each expression */
	add_ast_node(parser->container, &stmt->for_stmt.expr_list, parse_expression(parser)); /* initial value */
	checknext(ls, ',');
	add_ast_node(parser->container, &stmt->for_stmt.expr_list, parse_expression(parser)); /* limit */
	if (testnext(ls, ',')) {
		add_ast_node(parser->container, &stmt->for_stmt.expr_list,
			     parse_expression(parser)); /* optional step */
	}
	parse_forbody(parser, stmt, line, 1, 1);
}

/* parse a generic for loop */
static void parse_for_list(struct parser_state *parser, struct ast_node *stmt, const struct string_object *indexname)
{
	struct lexer_state *ls = parser->ls;
	/* forlist -> NAME {,NAME} IN explist forbody */
	int nvars = 4; /* gen, state, control, plus at least one declared var */
	/* create declared variables */
	struct lua_symbol *local = new_local_symbol(parser, indexname, RAVI_TANY, NULL);
	add_symbol(parser->container, &stmt->for_stmt.symbols, local);
	add_local_symbol_to_current_scope(parser, local);
	while (testnext(ls, ',')) {
		local = new_local_symbol(parser, check_name_and_next(ls), RAVI_TANY, NULL);
		add_symbol(parser->container, &stmt->for_stmt.symbols, local);
		add_local_symbol_to_current_scope(parser, local);
		nvars++;
	}
	checknext(ls, TOK_IN);
	parse_expression_list(parser, &stmt->for_stmt.expr_list);
	int line = ls->linenumber;
	parse_forbody(parser, stmt, line, nvars - 3, 0);
}

/* initial parsing of a for loop - calls fornum() or forlist() */
static struct ast_node *parse_for_statement(struct parser_state *parser, int line)
{
	struct lexer_state *ls = parser->ls;
	/* forstat -> FOR (fornum | forlist) END */
	const struct string_object *varname;
	struct ast_node *stmt = allocate_ast_node(parser, AST_NONE);
	stmt->for_stmt.symbols = NULL;
	stmt->for_stmt.expr_list = NULL;
	stmt->for_stmt.for_body = NULL;
	stmt->for_stmt.for_statement_list = NULL;
	stmt->for_stmt.for_scope = new_scope(parser);	// For the loop variables
	raviX_next(ls);			   /* skip 'for' */
	varname = check_name_and_next(ls); /* first variable name */
	switch (ls->t.token) {
	case '=':
		stmt->type = STMT_FOR_NUM;
		parse_fornum_statement(parser, stmt, varname, line);
		break;
	case ',':
	case TOK_IN:
		stmt->type = STMT_FOR_IN;
		parse_for_list(parser, stmt, varname);
		break;
	default:
		raviX_syntaxerror(ls, "'=' or 'in' expected");
	}
	check_match(ls, TOK_END, TOK_FOR, line);
	end_scope(parser);
	return stmt;
}

/* parse if cond then block - called from parse_if_statement() */
static struct ast_node *parse_if_cond_then_block(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* test_then_block -> [IF | ELSEIF] cond THEN block */
	raviX_next(ls); /* skip IF or ELSEIF */
	struct ast_node *test_then_block = allocate_ast_node(parser, STMT_TEST_THEN);				       // This is not an AST node on its own
	test_then_block->test_then_block.condition = parse_expression(parser); /* read condition */
	test_then_block->test_then_block.test_then_scope = NULL;
	test_then_block->test_then_block.test_then_statement_list = NULL;
	checknext(ls, TOK_THEN);
	if (ls->t.token == TOK_GOTO || ls->t.token == TOK_BREAK) {
		test_then_block->test_then_block.test_then_scope = new_scope(parser);
		struct ast_node *stmt = parse_goto_statment(parser); /* handle goto/break */
		add_ast_node(parser->container, &test_then_block->test_then_block.test_then_statement_list, stmt);
		skip_noop_statements(parser); /* skip other no-op statements */
		if (block_follow(ls, 0)) {    /* 'goto' is the entire block? */
			end_scope(parser);
			return test_then_block; /* and that is it */
		} else {			/* must skip over 'then' part if condition is false */
			;
		}
	} else { /* regular case (not goto/break) */
		test_then_block->test_then_block.test_then_scope = new_scope(parser);
	}
	parse_statement_list(parser, &test_then_block->test_then_block.test_then_statement_list); /* 'then' part */
	end_scope(parser);
	return test_then_block;
}

static struct ast_node *parse_if_statement(struct parser_state *parser, int line)
{
	struct lexer_state *ls = parser->ls;
	/* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
	struct ast_node *stmt = allocate_ast_node(parser, STMT_IF);
	stmt->if_stmt.if_condition_list = NULL;
	stmt->if_stmt.else_block = NULL;
	stmt->if_stmt.else_statement_list = NULL;
	struct ast_node *test_then_block = parse_if_cond_then_block(parser); /* IF cond THEN block */
	add_ast_node(parser->container, &stmt->if_stmt.if_condition_list, test_then_block);
	while (ls->t.token == TOK_ELSEIF) {
		test_then_block = parse_if_cond_then_block(parser); /* ELSEIF cond THEN block */
		add_ast_node(parser->container, &stmt->if_stmt.if_condition_list, test_then_block);
	}
	if (testnext(ls, TOK_ELSE))
		stmt->if_stmt.else_block = parse_block(parser, &stmt->if_stmt.else_statement_list); /* 'else' part */
	check_match(ls, TOK_END, TOK_IF, line);
	return stmt;
}

static struct ast_node *parse_local_function_statement(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	struct lua_symbol *symbol =
	    new_local_symbol(parser, check_name_and_next(ls), RAVI_TFUNCTION, NULL); /* new local variable */
	/* local function f ... is parsed as local f; f = function ... */
	add_local_symbol_to_current_scope(parser, symbol);
	struct ast_node *function_ast = new_function(parser);
	parse_function_body(parser, function_ast, 0, ls->linenumber); /* function created in next register */
	end_function(parser);
	struct ast_node *stmt = allocate_ast_node(parser, STMT_LOCAL);
	stmt->local_stmt.var_list = NULL;
	stmt->local_stmt.expr_list = NULL;
	add_symbol(parser->container, &stmt->local_stmt.var_list, symbol);
	add_ast_node(parser->container, &stmt->local_stmt.expr_list, function_ast);
	return stmt;
}

static struct ast_node *parse_local_statement(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* stat -> LOCAL NAME {',' NAME} ['=' explist] */
	struct ast_node *node = allocate_ast_node(parser, STMT_LOCAL);
	node->local_stmt.var_list = NULL;
	node->local_stmt.expr_list = NULL;
	int nvars = 0;
	do {
		/* local name : type = value */
		struct lua_symbol *symbol = parse_local_variable_declaration(parser);
		add_symbol(parser->container, &node->local_stmt.var_list, symbol);
		nvars++;
		if (nvars >= MAXVARS)
			raviX_syntaxerror(ls, "too many local variables");
	} while (testnext(ls, ','));
	if (testnext(ls, '=')) /* nexps = */
		parse_expression_list(parser, &node->local_stmt.expr_list);
	else {
		/* nexps = 0; */
		;
	}
	/* local symbols are only added to scope at the end of the local statement */
	struct lua_symbol *sym = NULL;
	FOR_EACH_PTR(node->local_stmt.var_list, sym) { add_local_symbol_to_current_scope(parser, sym); }
	END_FOR_EACH_PTR(sym);
	return node;
}

/* parse a function name specification with base symbol, optional selectors and optional method name
 */
static struct ast_node *parse_function_name(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* funcname -> NAME {fieldsel} [':' NAME] */
	struct ast_node *function_stmt = allocate_ast_node(parser, STMT_FUNCTION);
	function_stmt->function_stmt.function_expr = NULL;
	function_stmt->function_stmt.method_name = NULL;
	function_stmt->function_stmt.selectors = NULL;
	function_stmt->function_stmt.name = new_symbol_reference(parser);
	while (ls->t.token == '.') {
		add_ast_node(parser->container, &function_stmt->function_stmt.selectors, parse_field_selector(parser));
	}
	if (ls->t.token == ':') {
		function_stmt->function_stmt.method_name = parse_field_selector(parser);
	}
	return function_stmt;
}

static struct ast_node *parse_function_statement(struct parser_state *parser, int line)
{
	struct lexer_state *ls = parser->ls;
	/* funcstat -> FUNCTION funcname body */
	raviX_next(ls); /* skip FUNCTION */
	struct ast_node *function_stmt = parse_function_name(parser);
	int ismethod = function_stmt->function_stmt.method_name != NULL;
	struct ast_node *function_ast = new_function(parser);
	parse_function_body(parser, function_ast, ismethod, line);
	end_function(parser);
	function_stmt->function_stmt.function_expr = function_ast;
	return function_stmt;
}

/* parse function call with no returns or assignment statement */
static struct ast_node *parse_expression_statement(struct parser_state *parser)
{
	struct ast_node *stmt = allocate_ast_node(parser, STMT_EXPR);
	stmt->expression_stmt.var_expr_list = NULL;
	stmt->expression_stmt.expr_list = NULL;
	struct lexer_state *ls = parser->ls;
	/* stat -> func | assignment */
	/* Until we see '=' we do not know if this is an assignment or expr list*/
	struct ast_node_list *current_list = NULL;
	add_ast_node(parser->container, &current_list, parse_suffixed_expression(parser));
	while (testnext(ls, ',')) { /* assignment -> ',' suffixedexp assignment */
		add_ast_node(parser->container, &current_list, parse_suffixed_expression(parser));
	}
	if (ls->t.token == '=') { /* stat -> assignment ? */
		checknext(ls, '=');
		stmt->expression_stmt.var_expr_list = current_list;
		current_list = NULL;
		parse_expression_list(parser, &current_list);
	}
	stmt->expression_stmt.expr_list = current_list;
	// TODO Check that if not assignment then it is a function call
	return stmt;
}

static struct ast_node *parse_return_statement(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	/* stat -> RETURN [explist] [';'] */
	struct ast_node *return_stmt = allocate_ast_node(parser, STMT_RETURN);
	return_stmt->return_stmt.expr_list = NULL;
	if (block_follow(ls, 1) || ls->t.token == ';')
		/* nret = 0*/; /* return no values */
	else {
		/*nret = */
		parse_expression_list(parser, &return_stmt->return_stmt.expr_list); /* optional return values */
	}
	testnext(ls, ';'); /* skip optional semicolon */
	return return_stmt;
}

static struct ast_node *parse_do_statement(struct parser_state *parser, int line)
{
	raviX_next(parser->ls); /* skip DO */
	struct ast_node *stmt = allocate_ast_node(parser, STMT_DO);
	stmt->do_stmt.do_statement_list = NULL;
	stmt->do_stmt.scope = parse_block(parser, &stmt->do_stmt.do_statement_list);
	check_match(parser->ls, TOK_END, TOK_DO, line);
	return stmt;
}

/* parse a statement */
static struct ast_node *parse_statement(struct parser_state *parser)
{
	struct lexer_state *ls = parser->ls;
	int line = ls->linenumber; /* may be needed for error messages */
	struct ast_node *stmt = NULL;
	switch (ls->t.token) {
	case ';': {		/* stat -> ';' (empty statement) */
		raviX_next(ls); /* skip ';' */
		break;
	}
	case TOK_IF: { /* stat -> ifstat */
		stmt = parse_if_statement(parser, line);
		break;
	}
	case TOK_WHILE: { /* stat -> whilestat */
		stmt = parse_while_statement(parser, line);
		break;
	}
	case TOK_DO: { /* stat -> DO block END */
		stmt = parse_do_statement(parser, line);
		break;
	}
	case TOK_FOR: { /* stat -> forstat */
		stmt = parse_for_statement(parser, line);
		break;
	}
	case TOK_REPEAT: { /* stat -> repeatstat */
		stmt = parse_repeat_statement(parser, line);
		break;
	}
	case TOK_FUNCTION: { /* stat -> funcstat */
		stmt = parse_function_statement(parser, line);
		break;
	}
	case TOK_LOCAL: {		       /* stat -> localstat */
		raviX_next(ls);		       /* skip LOCAL */
		if (testnext(ls, TOK_FUNCTION)) /* local function? */
			stmt = parse_local_function_statement(parser);
		else
			stmt = parse_local_statement(parser);
		break;
	}
	case TOK_DBCOLON: {	/* stat -> label */
		raviX_next(ls); /* skip double colon */
		stmt = parse_label_statement(parser, check_name_and_next(ls), line);
		break;
	}
	case TOK_RETURN: {	/* stat -> retstat */
		raviX_next(ls); /* skip RETURN */
		stmt = parse_return_statement(parser);
		break;
	}
	case TOK_BREAK:	/* stat -> breakstat */
	case TOK_GOTO: { /* stat -> 'goto' NAME */
		stmt = parse_goto_statment(parser);
		break;
	}
	default: { /* stat -> func | assignment */
		stmt = parse_expression_statement(parser);
		break;
	}
	}
	return stmt;
}

/* Parses a sequence of statements */
/* statlist -> { stat [';'] } */
static void parse_statement_list(struct parser_state *parser, struct ast_node_list **list)
{
	struct lexer_state *ls = parser->ls;
	while (!block_follow(ls, 1)) {
		bool was_return = ls->t.token == TOK_RETURN;
		struct ast_node *stmt = parse_statement(parser);
		if (stmt)
			add_ast_node(parser->container, list, stmt);
		if (was_return)
			break; /* 'return' must be last statement */
	}
}

/* Starts a new scope. If the current function has no main block
 * defined then the new scope becomes its main block. The new scope
 * gets existing scope as its parent even if that belongs to parent
 * function.
 */
static struct block_scope *new_scope(struct parser_state *parser)
{
	struct compiler_state *container = parser->container;
	struct block_scope *scope = raviX_allocator_allocate(&container->block_scope_allocator, 0);
	scope->symbol_list = NULL;
	// scope->do_statement_list = NULL;
	scope->function = parser->current_function;
	assert(scope->function && scope->function->type == EXPR_FUNCTION);
	scope->parent = parser->current_scope;
	parser->current_scope = scope;
	if (!parser->current_function->function_expr.main_block)
		parser->current_function->function_expr.main_block = scope;
	return scope;
}

static void end_scope(struct parser_state *parser)
{
	assert(parser->current_scope);
	struct block_scope *scope = parser->current_scope;
	parser->current_scope = scope->parent;
	assert(parser->current_scope != NULL || scope == parser->current_function->function_expr.main_block);
}

/* Creates a new function AST node and starts the function scope.
New function becomes child of current function if any, and scope is linked
to previous scope which may be of parent function.
*/
static struct ast_node *new_function(struct parser_state *parser)
{
	struct ast_node *node = allocate_ast_node(parser, EXPR_FUNCTION);
	set_type(&node->function_expr.type, RAVI_TFUNCTION);
	node->function_expr.is_method = false;
	node->function_expr.is_vararg = false;
	node->function_expr.args = NULL;
	node->function_expr.child_functions = NULL;
	node->function_expr.upvalues = NULL;
	node->function_expr.locals = NULL;
	node->function_expr.main_block = NULL;
	node->function_expr.function_statement_list = NULL;
	node->function_expr.parent_function = parser->current_function;
	if (parser->current_function) {
		// Make this function a child of current function
		add_ast_node(parser->container, &parser->current_function->function_expr.child_functions, node);
	}
	parser->current_function = node;
	new_scope(parser); /* Start function scope */
	return node;
}

/* Ends the function node and closes the scope for the function. The
 * function being closed becomes the current AST node, while parent function/scope
 * become current function/scope.
 */
static struct ast_node *end_function(struct parser_state *parser)
{
	assert(parser->current_function);
	end_scope(parser);
	struct ast_node *function = parser->current_function;
	parser->current_function = function->function_expr.parent_function;
	return function;
}

/* mainfunc() equivalent - parses a Lua script, also known as chunk.
The code is wrapped in a vararg function */
static void parse_lua_chunk(struct parser_state *parser)
{
	raviX_next(parser->ls);					 /* read first token */
	parser->container->main_function = new_function(parser); /* vararg function wrapper */
	parser->container->main_function->function_expr.is_vararg = true;
	parse_statement_list(parser, &parser->container->main_function->function_expr.function_statement_list);
	end_function(parser);
	assert(parser->current_function == NULL);
	assert(parser->current_scope == NULL);
	check(parser->ls, TOK_EOS);
}

static void parser_state_init(struct parser_state *parser, struct lexer_state *ls, struct compiler_state *container)
{
	parser->ls = ls;
	parser->container = container;
	parser->current_function = NULL;
	parser->current_scope = NULL;
}

/*
** Parse the given source 'chunk' and build an abstract
** syntax tree; return 0 on success / non-zero return code on
** failure
*/
int raviX_parse(struct compiler_state *container, const char *buffer, size_t buflen, const char *name)
{
	struct lexer_state *lexstate = raviX_init_lexer(container, buffer, buflen, name);
	struct parser_state parser_state;
	parser_state_init(&parser_state, lexstate, container);
	int rc = setjmp(container->env);
	if (rc == 0) {
		parse_lua_chunk(&parser_state);
	}
	raviX_destroy_lexer(lexstate);
	return rc;
}

/*
Return true if two strings are equal, false otherwise.
*/
static int string_equal(const void *a, const void *b)
{
	const struct string_object *c1 = (const struct string_object *)a;
	const struct string_object *c2 = (const struct string_object *)b;
	if (c1->len != c2->len || c1->hash != c2->hash)
		return 0;
	return memcmp(c1->str, c2->str, c1->len) == 0;
}

static uint32_t string_hash(const void *c)
{
	const struct string_object *c1 = (const struct string_object *)c;
	return c1->hash;
}

struct compiler_state *raviX_init_compiler()
{
	struct compiler_state *container = (struct compiler_state *)calloc(1, sizeof(struct compiler_state));
	raviX_allocator_init(&container->ast_node_allocator, "ast nodes", sizeof(struct ast_node), sizeof(double),
			     sizeof(struct ast_node) * 32);
	raviX_allocator_init(&container->ptrlist_allocator, "ptrlists", sizeof(struct ptr_list), sizeof(double),
			     sizeof(struct ptr_list) * 32);
	raviX_allocator_init(&container->block_scope_allocator, "block scopes", sizeof(struct block_scope),
			     sizeof(double), sizeof(struct block_scope) * 32);
	raviX_allocator_init(&container->symbol_allocator, "symbols", sizeof(struct lua_symbol), sizeof(double),
			     sizeof(struct lua_symbol) * 64);
	raviX_allocator_init(&container->string_allocator, "strings", 0, sizeof(double), 1024);
	raviX_allocator_init(&container->string_object_allocator, "string_objects", sizeof(struct string_object),
			     sizeof(double), sizeof(struct string_object) * 64);
	raviX_buffer_init(&container->buff, 1024);
	container->strings = set_create(string_hash, string_equal);
	container->main_function = NULL;
	container->killed = false;
	container->linearizer = NULL;
	return container;
}

// static void show_allocations(struct compiler_state *compiler)
//{
//	raviX_allocator_show_allocations(&compiler->symbol_allocator);
//	raviX_allocator_show_allocations(&compiler->block_scope_allocator);
//	raviX_allocator_show_allocations(&compiler->ast_node_allocator);
//	raviX_allocator_show_allocations(&compiler->ptrlist_allocator);
//	raviX_allocator_show_allocations(&compiler->string_allocator);
//	raviX_allocator_show_allocations(&compiler->string_object_allocator);
//}

void raviX_destroy_compiler(struct compiler_state *container)
{
	if (!container->killed) {
		// show_allocations(container);
		if (container->linearizer) {
			raviX_destroy_linearizer(container->linearizer);
			free(container->linearizer);
		}
		set_destroy(container->strings, NULL);
		raviX_buffer_free(&container->buff);
		raviX_allocator_destroy(&container->symbol_allocator);
		raviX_allocator_destroy(&container->block_scope_allocator);
		raviX_allocator_destroy(&container->ast_node_allocator);
		raviX_allocator_destroy(&container->ptrlist_allocator);
		raviX_allocator_destroy(&container->string_allocator);
		raviX_allocator_destroy(&container->string_object_allocator);
		container->killed = true;
	}
}

