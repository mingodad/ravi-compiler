#ifndef ravicomp_RAVIAPI_H
#define ravicomp_RAVIAPI_H

#include "ravicomp_export.h"
#include "ravi_compiler.h"

#include <stdlib.h>

struct Ravi_CompilerInterface {
	/* ------------------------ Inputs ------------------------------ */
	void *context; /* Ravi supplied context */

	const char *source; /* Source code to be compiled - managed by Ravi */
	size_t source_len; /* Size of source code */
	const char *source_name; /* Name of the source */

	char main_func_name[31]; /* Name of the generated function that when called will set up the Lua closure */

	/* ------------------------- Outputs ------------------------------ */
	const char* generated_code;  /* Output of the compiler, must be freed by caller. */

	/* ------------------------ Debugging and error handling ----------------------------------------- */
	void (*debug_message)(void *context, const char *filename, long long line, const char *message);
	void (*error_message)(void *context, const char *message);
};

/**
 * This is the API exposed by the Compiler itself. This function is invoked by
 * Ravi when it is necessary to compile some Ravi code.
 * @param compiler_interface The interface expected by the compiler must be setup
 * @return 0 for success, non-zero for failure
 */
RAVICOMP_EXPORT int raviX_compile(struct Ravi_CompilerInterface *compiler_interface);

#endif
