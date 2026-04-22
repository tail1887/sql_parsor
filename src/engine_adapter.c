#include "engine_adapter.h"

#include "lexer.h"
#include "sql_processor.h"

#include <pthread.h>
#include <string.h>

static pthread_rwlock_t g_engine_lock = PTHREAD_RWLOCK_INITIALIZER;

static size_t skip_utf8_bom(const char *sql, size_t len) {
    if (len >= 3 && (unsigned char)sql[0] == 0xEF && (unsigned char)sql[1] == 0xBB &&
        (unsigned char)sql[2] == 0xBF) {
        return 3;
    }
    return 0;
}

static SqlStatementType detect_statement_type(const char *sql) {
    Lexer lex;
    Token token = {0};
    size_t len = 0;
    size_t start = 0;

    if (!sql) {
        return SQL_STATEMENT_UNKNOWN;
    }

    len = strlen(sql);
    start = skip_utf8_bom(sql, len);
    lexer_init(&lex, sql + start, len - start);
    if (lexer_next(&lex, &token) != 0) {
        return SQL_STATEMENT_UNKNOWN;
    }

    if (token.kind == TOKEN_SELECT) {
        return SQL_STATEMENT_SELECT;
    }
    if (token.kind == TOKEN_INSERT) {
        return SQL_STATEMENT_INSERT;
    }
    return SQL_STATEMENT_UNKNOWN;
}

int engine_adapter_execute_sql(const char *sql, SqlExecutionResult *out) {
    int rc = 0;
    SqlStatementType type = detect_statement_type(sql);

    if (type == SQL_STATEMENT_SELECT) {
        pthread_rwlock_rdlock(&g_engine_lock);
    } else {
        pthread_rwlock_wrlock(&g_engine_lock);
    }

    rc = sql_processor_run_text(sql, out, NULL);
    pthread_rwlock_unlock(&g_engine_lock);

    return rc;
}
