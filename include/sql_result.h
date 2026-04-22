#ifndef SQL_RESULT_H
#define SQL_RESULT_H

#include <stddef.h>

typedef enum {
    SQL_STATEMENT_UNKNOWN = 0,
    SQL_STATEMENT_INSERT,
    SQL_STATEMENT_SELECT,
} SqlStatementType;

typedef struct {
    SqlStatementType statement_type;
    int exit_code;
    char *message;
    size_t affected_rows;
    char **columns;
    size_t column_count;
    char ***rows;
    size_t row_count;
} SqlExecutionResult;

const char *sql_statement_type_name(SqlStatementType type);
void sql_execution_result_clear(SqlExecutionResult *result);

#endif
