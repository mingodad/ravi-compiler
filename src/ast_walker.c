#include <ravi_compiler.h>

#include <ravi_ast.h>

const struct function_expression *raviX_ast_get_main_function(const struct compiler_state *compiler_state)
{
	return &compiler_state->main_function->function_expr;
}
const struct var_type *raviX_function_type(const struct function_expression *function_expression)
{
	return &function_expression->type;
}
bool raviX_function_is_vararg(const struct function_expression *function_expression)
{
	return function_expression->is_vararg;
}
bool raviX_function_is_method(const struct function_expression *function_expression)
{
	return function_expression->is_method;
}
const struct function_expression *raviX_function_parent(const struct function_expression *function_expression)
{
	if (function_expression->parent_function == NULL)
		return NULL;
	else
		return &function_expression->parent_function->function_expr;
}
void raviX_function_foreach_child(const struct function_expression *function_expression, void *userdata,
				  void (*callback)(void *userdata,
						   const struct function_expression *function_expression))
{
	struct ast_node *node;
	FOR_EACH_PTR(function_expression->child_functions, node) { callback(userdata, &node->function_expr); }
	END_FOR_EACH_PTR(node)
}
struct block_scope *raviX_function_scope(const struct function_expression *function_expression)
{
	return function_expression->main_block;
}
void raviX_function_foreach_statement(const struct function_expression *function_expression, void *userdata,
				      void (*callback)(void *userdata, const struct statement *statement))
{
	struct ast_node *node;
	FOR_EACH_PTR(function_expression->function_statement_list, node)
	{
		assert(node->type <= AST_EXPR_STMT);
		callback(userdata, (struct statement *)node);
	}
	END_FOR_EACH_PTR(node)
}
enum ast_node_type raviX_statement_type(struct statement *statement) { return statement->type; }
void raviX_function_foreach_argument(const struct function_expression *function_expression, void *userdata,
				     void (*callback)(void *userdata, const struct lua_variable_symbol *symbol))
{
	struct lua_symbol *symbol;
	FOR_EACH_PTR(function_expression->args, symbol) { callback(userdata, &symbol->variable); }
	END_FOR_EACH_PTR(symbol)
}
void raviX_function_foreach_local(const struct function_expression *function_expression, void *userdata,
				  void (*callback)(void *userdata, const struct lua_variable_symbol *lua_local_symbol))
{
	struct lua_symbol *symbol;
	FOR_EACH_PTR(function_expression->locals, symbol) { callback(userdata, &symbol->variable); }
	END_FOR_EACH_PTR(symbol)
}
void raviX_function_foreach_upvalue(const struct function_expression *function_expression, void *userdata,
				    void (*callback)(void *userdata, const struct lua_upvalue_symbol *symbol))
{
	struct lua_symbol *symbol;
	FOR_EACH_PTR(function_expression->upvalues, symbol) { callback(userdata, &symbol->upvalue); }
	END_FOR_EACH_PTR(symbol)
}

const struct string_object *raviX_local_symbol_name(const struct lua_variable_symbol *symbol)
{
	return symbol->var_name;
}

const struct var_type *raviX_local_symbol_type(const struct lua_variable_symbol *symbol) { return &symbol->value_type; }

const struct block_scope *raviX_local_symbol_scope(const struct lua_variable_symbol *symbol) { return symbol->block; }

#define n(v) ((struct ast_node *)v)
const struct return_statement *raviX_return_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_RETURN_STMT);
	return &n(stmt)->return_stmt;
}
const struct label_statement *raviX_label_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_LABEL_STMT);
	return &n(stmt)->label_stmt;
}
const struct goto_statement *raviX_goto_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_GOTO_STMT);
	return &n(stmt)->goto_stmt;
}
const struct local_statement *raviX_local_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_LOCAL_STMT);
	return &n(stmt)->local_stmt;
}
const struct expression_statement *raviX_expression_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_EXPR_STMT);
	return &n(stmt)->expression_stmt;
}
const struct function_statement *raviX_function_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_FUNCTION_STMT);
	return &n(stmt)->function_stmt;
}
const struct do_statement *raviX_do_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_DO_STMT);
	return &n(stmt)->do_stmt;
}
const struct test_then_statement *raviX_test_then_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_TEST_THEN_STMT);
	return &n(stmt)->test_then_block;
}
const struct if_statement *raviX_if_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_IF_STMT);
	return &n(stmt)->if_stmt;
}
const struct while_or_repeat_statement *raviX_while_or_repeat_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_WHILE_STMT || stmt->type == AST_REPEAT_STMT);
	return &n(stmt)->while_or_repeat_stmt;
}
const struct for_statement *raviX_for_statement(const struct statement *stmt)
{
	assert(stmt->type == AST_FORIN_STMT || stmt->type == AST_FORNUM_STMT);
	return &n(stmt)->for_stmt;
}
const struct literal_expression *raviX_literal_expression(const struct expression *expr)
{
	assert(expr->type == AST_LITERAL_EXPR);
	return &n(expr)->literal_expr;
}
const struct symbol_expression *raviX_symbol_expression(const struct expression *expr)
{
	assert(expr->type == AST_SYMBOL_EXPR);
	return &n(expr)->symbol_expr;
}
const struct index_expression *raviX_index_expression(const struct expression *expr)
{
	assert(expr->type == AST_Y_INDEX_EXPR || expr->type == AST_FIELD_SELECTOR_EXPR);
	return &n(expr)->index_expr;
}
const struct unary_expression *raviX_unary_expression(const struct expression *expr)
{
	assert(expr->type == AST_UNARY_EXPR);
	return &n(expr)->unary_expr;
}
const struct binary_expression *raviX_binary_expression(const struct expression *expr)
{
	assert(expr->type == AST_BINARY_EXPR);
	return &n(expr)->binary_expr;
}
const struct function_expression *raviX_function_expression(const struct expression *expr)
{
	assert(expr->type == AST_FUNCTION_EXPR);
	return &n(expr)->function_expr;
}
const struct table_element_assignment_expression *
raviX_table_element_assignment_expression(const struct expression *expr)
{
	assert(expr->type == AST_INDEXED_ASSIGN_EXPR);
	return &n(expr)->table_elem_assign_expr;
}
const struct table_literal_expression *raviX_table_literal_expression(const struct expression *expr)
{
	assert(expr->type == AST_TABLE_EXPR);
	return &n(expr)->table_expr;
}
const struct suffixed_expression *raviX_suffixed_expression(const struct expression *expr)
{
	assert(expr->type == AST_SUFFIXED_EXPR);
	return &n(expr)->suffixed_expr;
}
const struct function_call_expression *raviX_function_call_expression(const struct expression *expr)
{
	assert(expr->type == AST_FUNCTION_CALL_EXPR);
	return &n(expr)->function_call_expr;
}
#undef n

void raviX_return_statement_foreach_expression(const struct return_statement *statement, void *userdata,
					       void (*callback)(void *, const struct expression *expr))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->expr_list, node)
	{
		assert(node->type >= AST_LITERAL_EXPR && node->type <= AST_FUNCTION_CALL_EXPR);
		callback(userdata, (struct expression *)node);
	}
	END_FOR_EACH_PTR(node)
}

const struct string_object *raviX_label_statement_label_name(const struct label_statement *statement)
{
	return statement->symbol->label.label_name;
}
const struct block_scope *raviX_label_statement_label_scope(const struct label_statement *statement)
{
	return statement->symbol->label.block;
}

const struct string_object *raviX_goto_statement_label_name(const struct goto_statement *statement)
{
	return statement->name;
}
const struct block_scope *raviX_goto_statement_scope(const struct goto_statement *statement)
{
	return statement->goto_scope;
}
bool raviX_goto_statement_is_break(const struct goto_statement *statement) { return statement->is_break; }

void raviX_local_statement_foreach_expression(const struct local_statement *statement, void *userdata,
					      void (*callback)(void *, const struct expression *expr))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->expr_list, node)
	{
		assert(node->type >= AST_LITERAL_EXPR && node->type <= AST_FUNCTION_CALL_EXPR);
		callback(userdata, (struct expression *)node);
	}
	END_FOR_EACH_PTR(node)
}
void raviX_local_statement_foreach_symbol(const struct local_statement *statement, void *userdata,
					  void (*callback)(void *, const struct lua_variable_symbol *expr))
{
	struct lua_symbol *symbol;
	FOR_EACH_PTR(statement->var_list, symbol)
	{
		assert(symbol->symbol_type == SYM_LOCAL);
		callback(userdata, &symbol->variable);
	}
	END_FOR_EACH_PTR(node)
}
void raviX_expression_statement_foreach_lhs_expression(const struct expression_statement *statement, void *userdata,
						       void (*callback)(void *, const struct expression *expr))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->var_expr_list, node)
	{
		assert(node->type >= AST_LITERAL_EXPR && node->type <= AST_FUNCTION_CALL_EXPR);
		callback(userdata, (struct expression *)node);
	}
	END_FOR_EACH_PTR(node)
}
void raviX_expression_statement_foreach_rhs_expression(const struct expression_statement *statement, void *userdata,
						       void (*callback)(void *, const struct expression *expr))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->expr_list, node)
	{
		assert(node->type >= AST_LITERAL_EXPR && node->type <= AST_FUNCTION_CALL_EXPR);
		callback(userdata, (struct expression *)node);
	}
	END_FOR_EACH_PTR(node)
}
const struct symbol_expression *raviX_function_statement_name(const struct function_statement *statement)
{
	assert(statement->name->type == AST_SYMBOL_EXPR);
	return &statement->name->symbol_expr;
}
bool raviX_function_statement_is_method(const struct function_statement *statement)
{
	return statement->method_name != NULL;
}
const struct index_expression *raviX_function_statement_method_name(const struct function_statement *statement)
{
	assert(statement->method_name->type == AST_Y_INDEX_EXPR ||
	       statement->method_name->type == AST_FIELD_SELECTOR_EXPR);
	return &statement->method_name->index_expr;
}
bool raviX_function_statement_has_selectors(const struct function_statement *statement)
{
	return statement->selectors != NULL;
}
void raviX_function_statement_foreach_selector(const struct function_statement *statement, void *userdata,
					       void (*callback)(void *, const struct index_expression *expr))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->selectors, node)
	{
		assert(node->type == AST_Y_INDEX_EXPR || node->type == AST_FIELD_SELECTOR_EXPR);
		callback(userdata, &node->index_expr);
	}
	END_FOR_EACH_PTR(node)
}
const struct function_expression *raviX_function_ast(const struct function_statement *statement)
{
	assert(statement->function_expr->type == AST_FUNCTION_EXPR);
	return &statement->function_expr->function_expr;
}
const struct block_scope *raviX_do_statement_scope(const struct do_statement *statement) { return statement->scope; }
void raviX_do_statement_foreach_statement(const struct do_statement *statement, void *userdata,
					  void (*callback)(void *userdata, const struct statement *statement))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->do_statement_list, node)
	{
		assert(node->type <= AST_EXPR_STMT);
		callback(userdata, (struct statement *)node);
	}
	END_FOR_EACH_PTR(node)
}
const struct block_scope *raviX_test_then_statement_scope(const struct test_then_statement *statement)
{
	return statement->test_then_scope;
}
void raviX_test_the_statement_foreach_statement(const struct test_then_statement *statement, void *userdata,
						void (*callback)(void *userdata, const struct statement *statement))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->test_then_statement_list, node)
	{
		assert(node->type <= AST_EXPR_STMT);
		callback(userdata, (struct statement *)node);
	}
	END_FOR_EACH_PTR(node)
}
const struct expression *raviX_test_then_statement_condition(const struct test_then_statement *statement)
{
	assert(statement->condition->type >= AST_LITERAL_EXPR && statement->condition->type <= AST_FUNCTION_CALL_EXPR);
	return (struct expression *)statement->condition;
}
void raviX_if_statement_foreach_test_then_statement(const struct if_statement *statement, void *userdata,
						    void (*callback)(void *, const struct test_then_statement *stmt))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->if_condition_list, node)
	{
		assert(node->type == AST_TEST_THEN_STMT);
		callback(userdata, &node->test_then_block);
	}
	END_FOR_EACH_PTR(node)
}
const struct block_scope *raviX_if_then_statement_else_scope(const struct if_statement *statement)
{
	return statement->else_block;
}
void raviX_if_statement_foreach_else_statement(const struct if_statement *statement, void *userdata,
					       void (*callback)(void *userdata, const struct statement *statement))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->else_statement_list, node)
	{
		assert(node->type <= AST_EXPR_STMT);
		callback(userdata, (struct statement *)node);
	}
	END_FOR_EACH_PTR(node)
}

const struct expression *raviX_while_or_repeat_statement_condition(const struct while_or_repeat_statement *statement)
{
	assert(statement->condition->type >= AST_LITERAL_EXPR && statement->condition->type <= AST_FUNCTION_CALL_EXPR);
	return (struct expression *)statement->condition;
}
const struct block_scope *raviX_while_or_repeat_statement_scope(const struct while_or_repeat_statement *statement)
{
	return statement->loop_scope;
}
void raviX_while_or_repeat_statement_foreach_statement(const struct while_or_repeat_statement *statement,
						       void *userdata,
						       void (*callback)(void *userdata,
									const struct statement *statement))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->loop_statement_list, node)
	{
		assert(node->type <= AST_EXPR_STMT);
		callback(userdata, (struct statement *)node);
	}
	END_FOR_EACH_PTR(node)
}
const struct block_scope *raviX_for_statement_scope(const struct for_statement *statement)
{
	return statement->for_scope;
}
void raviX_for_statement_foreach_symbol(const struct for_statement *statement, void *userdata,
					void (*callback)(void *, const struct lua_variable_symbol *expr))
{
	struct lua_symbol *symbol;
	FOR_EACH_PTR(statement->symbols, symbol)
	{
		assert(symbol->symbol_type == SYM_LOCAL);
		callback(userdata, &symbol->variable);
	}
	END_FOR_EACH_PTR(node)
}
void raviX_for_statement_foreach_expression(const struct for_statement *statement, void *userdata,
					    void (*callback)(void *, const struct expression *expr))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->expr_list, node)
	{
		assert(node->type >= AST_LITERAL_EXPR && node->type <= AST_FUNCTION_CALL_EXPR);
		callback(userdata, (struct expression *)node);
	}
	END_FOR_EACH_PTR(node)
}
const struct block_scope *raviX_for_statement_body_scope(const struct for_statement *statement)
{
	return statement->for_body;
}
void raviX_for_statement_body_foreach_statement(const struct for_statement *statement, void *userdata,
						void (*callback)(void *userdata, const struct statement *statement))
{
	struct ast_node *node;
	FOR_EACH_PTR(statement->for_statement_list, node)
	{
		assert(node->type <= AST_EXPR_STMT);
		callback(userdata, (struct statement *)node);
	}
	END_FOR_EACH_PTR(node)
}
const struct var_type *raviX_literal_expression_type(const struct literal_expression *expression)
{
	return &expression->type;
}
const SemInfo *raviX_literal_expression_literal(const struct literal_expression *expression) { return &expression->u; }
