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
