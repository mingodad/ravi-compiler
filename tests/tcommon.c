#include "tcommon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parse_arguments(struct arguments *args, int argc, const char *argv[])
{
	memset(args, 0, sizeof *args);
	args->typecheck = 1;
	args->linearize = 1;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--notypecheck") == 0) {
			args->typecheck = 0;
		} else if (strcmp(argv[i], "--nolinearize") == 0) {
			args->linearize = 0;
		} else if (strcmp(argv[i], "-f") == 0) {
			if (args->filename) {
				fprintf(stderr, "-f already accepted\n");
				continue;
			}
			if (i < argc - 1) {
				i++;
				args->filename = strdup(argv[i]);
			} else {
				fprintf(stderr, "Missing file name after -f\n");
				exit(1);
			}
		} else {
			if (args->code) {
				fprintf(stderr, "Bad argument at %d", i);
				exit(1);
			} else {
				args->code = strdup(argv[i]);
			}
		}
	}
}

const char *read_file(const char *filename)
{
	FILE *fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "Failed to open file %s\n", filename);
		return NULL;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fprintf(stderr, "Failed to seek to file end\n");
		fclose(fp);
		return NULL;
	}
	long long len = ftell(fp);
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fprintf(stderr, "Failed to seek to file beginning\n");
		fclose(fp);
		return NULL;
	}
	char *buffer = calloc(1, len + 10);
	size_t n = fread(buffer, 1, len, fp);
	if (n == 0) {
		fprintf(stderr, "Failed to read file\n");
		fclose(fp);
		free(buffer);
		return NULL;
	}
	fclose(fp);
	return buffer;
}



