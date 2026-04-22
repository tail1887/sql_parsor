#include "sql_result.h"

#include <stdlib.h>
#include <string.h>

const char *sql_statement_type_name(SqlStatementType type) {
    switch (type) {
    case SQL_STATEMENT_INSERT:
        return "insert";
    case SQL_STATEMENT_SELECT:
        return "select";
    default:
        return "unknown";
    }
}

void sql_execution_result_clear(SqlExecutionResult *result) {
    if (!result) {
        return;
    }

    for (size_t i = 0; i < result->column_count; i++) {
        free(result->columns[i]);
    }
    free(result->columns);

    for (size_t r = 0; r < result->row_count; r++) {
        if (!result->rows[r]) {
            continue;
        }
        for (size_t c = 0; c < result->column_count; c++) {
            free(result->rows[r][c]);
        }
        free(result->rows[r]);
    }
    free(result->rows);
    free(result->message);

    memset(result, 0, sizeof *result);
}
