#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

#include <stdio.h>

int execute_statement(
    const Statement *statement,
    const char *student_csv_path,
    const char *entry_log_bin_path,
    FILE *output_stream,
    FILE *error_stream);

#endif
