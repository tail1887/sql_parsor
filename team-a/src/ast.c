#include "ast.h"

#include <stdlib.h>

void ast_insert_stmt_free(InsertStmt *stmt) {
    if (!stmt) {
        return;
    }
    free(stmt->table);
    for (size_t i = 0; i < stmt->value_count; i++) {
        free(stmt->values[i].text);
    }
    free(stmt->values);
    free(stmt);
}

void ast_select_stmt_free(SelectStmt *stmt) {
    if (!stmt) {
        return;
    }
    for (size_t i = 0; i < stmt->column_count; i++) {
        free(stmt->columns[i]);
    }
    free(stmt->columns);
    free(stmt->table);
    free(stmt);
}
