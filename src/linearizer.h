#ifndef ravicomp_LINEARIZER_H
#define ravicomp_LINEARIZER_H

#include "ravi_compiler.h"

#include "common.h"
#include "parser.h"
#include "allocate.h"
#include "membuf.h"
#include "ptrlist.h"

/*
Linearizer component is responsible for translating the abstract syntax tree to
a Linear intermediate representation (IR).
*/
typedef struct Instruction Instruction;
struct basic_block;
struct proc;
struct constant;

DECLARE_PTR_LIST(InstructionList, Instruction);
DECLARE_PTR_LIST(PseudoList, struct pseudo);
DECLARE_PTR_LIST(ProcList, struct proc);

#define container_of(ptr, type, member) ((type *)((char *)(ptr)-offsetof(type, member)))

/* order is important here ! */
enum opcode {
	op_nop,
	op_ret,
	op_add,
	op_addff,
	op_addfi,
	op_addii,
	op_sub,
	op_subff,
	op_subfi,
	op_subif,
	op_subii,
	op_mul,
	op_mulff,
	op_mulfi,
	op_mulii,
	op_div,
	op_divff,
	op_divfi,
	op_divif,
	op_divii,
	op_idiv,
	op_band,
	op_bandii,
	op_bor,
	op_borii,
	op_bxor,
	op_bxorii,
	op_shl,
	op_shlii,
	op_shr,
	op_shrii,
	op_eq,
	op_eqii,
	op_eqff,
	op_lt,
	op_ltii,
	op_ltff,
	op_le,
	op_leii,
	op_leff,
	op_mod,
	op_pow,
	op_closure,
	op_unm,
	op_unmi,
	op_unmf,
	op_len,
	op_leni,
	op_toint,
	op_toflt,
	op_toclosure,
	op_tostring,
	op_toiarray,
	op_tofarray,
	op_totable,
	op_totype,
	op_not,
	op_bnot,
	op_loadglobal,
	op_newtable,
	op_newiarray,
	op_newfarray,
	op_put, /* target is any */
	op_put_ikey,
	op_put_skey,
	op_tput, /* target is table */
	op_tput_ikey,
	op_tput_skey,
	op_iaput, /* target is integer[]*/
	op_iaput_ival,
	op_faput, /* target is number[] */
	op_faput_fval,
	op_cbr,
	op_br,
	op_mov,
	op_movi,
	op_movif, /* int to float if compatible else error */
	op_movf,
	op_movfi, /* float to int if compatible else error */
	op_call,
	op_get,
	op_get_ikey,
	op_get_skey,
	op_tget,
	op_tget_ikey,
	op_tget_skey,
	op_iaget,
	op_iaget_ikey,
	op_faget,
	op_faget_ikey,
	op_storeglobal,
	op_close,
	op_string_concat
};

/*
* The IR instructions use operands and targets of type pseudo, which
* is a way of referencing several different types of objects.
*/
enum PseudoType {
	PSEUDO_SYMBOL, /* An object of type lua_symbol representing local var or upvalue, always refers to Lua stack relative to 'base' */
	PSEUDO_TEMP_FLT, /* A floating point temp - may also be used for locals that don't escape - refers to C var */
	PSEUDO_TEMP_INT, /* An integer temp - may also be used for locals that don't escape - refers to C var */
	PSEUDO_TEMP_BOOL, /* An integer temp but restricted to 1 and 0  - refers to C var, shares the virtual C stack with PSEUDO_TEMP_INT */
	PSEUDO_TEMP_ANY, /* A temp of any type - will always be on Lua stack relative to 'base' */
	PSEUDO_CONSTANT, /* A literal value */
	PSEUDO_PROC, /* A proc / function */
	PSEUDO_NIL, /* Literal */
	PSEUDO_TRUE, /* Literal */
	PSEUDO_FALSE, /* Literal */
	PSEUDO_BLOCK, /* Points to a basic block, used as targets for jumps */
	PSEUDO_RANGE, /* Represents a range of registers from a certain starting register on Lua stack relative to 'base' */
	PSEUDO_RANGE_SELECT, /* Picks a certain register from a range, resolves to register on Lua stack, relative to 'base' */
	/* TODO we need a type for var args */
	PSEUDO_LUASTACK /* Specifies a Lua stack position - not used by linearizer - for use by codegen. This is relative to CI->func rather than 'base' */
};

/* pseudo represents a pseudo (virtual) register */
struct pseudo {
	unsigned type : 4, regnum : 16, freed : 1;
	Instruction *insn; /* instruction that created this pseudo */
	union {
		LuaSymbol *symbol;	 /* PSEUDO_SYMBOL */
		const struct constant *constant; /* PSEUDO_CONSTANT */
		LuaSymbol *temp_for_local; /* PSEUDO_TEMP - if the temp represents a local */
		struct proc *proc;		 /* PSEUDO_PROC */
		struct basic_block *block;	 /* PSEUDO_BLOCK */
		struct pseudo *range_pseudo;	 /* PSEUDO_RANGE_SELECT */
		int stackidx; /* PSEUDO_LUASTACK */
	};
};

/* single instruction */
struct Instruction {
	unsigned opcode : 8;
	PseudoList *operands;
	PseudoList *targets;
	struct basic_block *block; /* owning block */
};

/* Basic block */
struct basic_block {
	nodeId_t index; /* The index of the block is a key to enable retrieving the block from its container */
	InstructionList *insns; /* Note that if number of instructions is 0 then the block was logically deleted */
};
DECLARE_PTR_LIST(BasicBlockList, struct basic_block);

struct pseudo_generator {
	uint8_t next_reg; /* Next register if no free registers, initially 0 */
	int16_t free_pos; /* number of values in free_regs */
	uint8_t free_regs[256]; /* list of free registers */
};

struct constant {
	uint8_t type;	/* ravitype_t RAVI_TNUMINT, RAVI_TNUMFLT or RAVI_TSTRING */
	uint16_t index; /* index number starting from 0 assigned to each constant - acts like a reg num.
			 * Each type will be assigned separate range */
	union {
		lua_Integer i;
		lua_Number n;
		const StringObject *s;
	};
};

/* proc is a type of cfg */
struct proc {
	unsigned node_count;
	unsigned allocated;
	struct basic_block **nodes;
	uint32_t id; /* ID for the proc */
	LinearizerState *linearizer;
	ProcList *procs;	/* procs defined in this proc */
	struct proc *parent;		/* enclosing proc */
	AstNode *function_expr; /* function ast that we are compiling */
	Scope *current_scope;
	struct basic_block *current_bb;
	struct basic_block *current_break_target; /* track the current break target, previous target must be saved /
						     restored in stack discipline */
	Scope *current_break_scope;  /* as above track the block scope */
	struct pseudo_generator local_pseudos;	  /* locals */
	struct pseudo_generator temp_int_pseudos; /* temporaries known to be integer type */
	struct pseudo_generator temp_flt_pseudos; /* temporaries known to be number type */
	struct pseudo_generator temp_pseudos;	  /* All other temporaries */
	struct set *constants;			  /* constants used by this proc */
	uint16_t num_intconstants;
	uint16_t num_fltconstants;
	uint16_t num_strconstants;
	struct graph *cfg;  /* place holder for control flow graph; the linearizer does not create this */
	char funcname[30]; /* Each proc needs a name inside a module - name is a short string */
	void *userdata; /* For use by code generator */
};

struct LinearizerState {
	struct allocator instruction_allocator;
	struct allocator pseudo_allocator;
	struct allocator ptrlist_allocator;
	struct allocator basic_block_allocator;
	struct allocator proc_allocator;
	struct allocator unsized_allocator;
	struct allocator constant_allocator;
	CompilerState *ast_container;
	struct proc *main_proc;	     /* The root of the compiled chunk of code */
	ProcList *all_procs; /* All procs allocated by the linearizer */
	struct proc *current_proc;   /* proc being compiled */
	uint32_t proc_id;
};

void raviX_show_linearizer(LinearizerState *linearizer, TextBuffer *mb);
void raviX_output_basic_block_as_table(struct proc *proc, struct basic_block *bb, TextBuffer *mb);

Instruction *raviX_last_instruction(struct basic_block *block);
struct pseudo* raviX_allocate_stack_pseudo(struct proc* proc, unsigned reg);
const char *raviX_opcode_name(unsigned int opcode);

#endif