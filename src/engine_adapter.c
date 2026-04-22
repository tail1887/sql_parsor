#include "engine_adapter.h"

#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "sql_processor.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned readers;
    unsigned waiting_writers;
    int writer_active;
} FairRwLock;

typedef struct TableLockEntry {
    char *table;
    FairRwLock lock;
    struct TableLockEntry *next;
} TableLockEntry;

typedef enum {
    ACCESS_NONE = 0,
    ACCESS_READ,
    ACCESS_WRITE,
} AccessMode;

typedef struct {
    SqlStatementType statement_type;
    AccessMode access_mode;
    char *table_name;
} StatementAccess;

#define FAIR_RWLOCK_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0}

static FairRwLock g_fallback_lock = FAIR_RWLOCK_INITIALIZER;
static pthread_mutex_t g_table_map_mutex = PTHREAD_MUTEX_INITIALIZER;
static TableLockEntry *g_table_locks = NULL;

static int64_t monotonic_now_ns(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static char *dup_cstr(const char *text) {
    size_t len = 0;
    char *copy = NULL;

    if (!text) {
        return NULL;
    }
    len = strlen(text);
    copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

static void fair_rwlock_init(FairRwLock *lock) {
    pthread_mutex_init(&lock->mutex, NULL);
    pthread_cond_init(&lock->cond, NULL);
    lock->readers = 0;
    lock->waiting_writers = 0;
    lock->writer_active = 0;
}

static void fair_rwlock_rdlock(FairRwLock *lock) {
    pthread_mutex_lock(&lock->mutex);
    while (lock->writer_active || lock->waiting_writers > 0) {
        pthread_cond_wait(&lock->cond, &lock->mutex);
    }
    lock->readers++;
    pthread_mutex_unlock(&lock->mutex);
}

static void fair_rwlock_wrlock(FairRwLock *lock) {
    pthread_mutex_lock(&lock->mutex);
    lock->waiting_writers++;
    while (lock->writer_active || lock->readers > 0) {
        pthread_cond_wait(&lock->cond, &lock->mutex);
    }
    lock->waiting_writers--;
    lock->writer_active = 1;
    pthread_mutex_unlock(&lock->mutex);
}

static void fair_rwlock_rdunlock(FairRwLock *lock) {
    pthread_mutex_lock(&lock->mutex);
    if (lock->readers > 0) {
        lock->readers--;
    }
    if (lock->readers == 0) {
        pthread_cond_broadcast(&lock->cond);
    }
    pthread_mutex_unlock(&lock->mutex);
}

static void fair_rwlock_wrunlock(FairRwLock *lock) {
    pthread_mutex_lock(&lock->mutex);
    lock->writer_active = 0;
    pthread_cond_broadcast(&lock->cond);
    pthread_mutex_unlock(&lock->mutex);
}

static TableLockEntry *find_table_lock_unlocked(const char *table_name) {
    TableLockEntry *entry = g_table_locks;

    while (entry) {
        if (strcmp(entry->table, table_name) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static FairRwLock *get_table_lock(const char *table_name) {
    TableLockEntry *entry = NULL;

    if (!table_name || table_name[0] == '\0') {
        return &g_fallback_lock;
    }

    pthread_mutex_lock(&g_table_map_mutex);
    entry = find_table_lock_unlocked(table_name);
    if (!entry) {
        entry = calloc(1, sizeof *entry);
        if (entry) {
            entry->table = dup_cstr(table_name);
            if (!entry->table) {
                free(entry);
                entry = NULL;
            } else {
                fair_rwlock_init(&entry->lock);
                entry->next = g_table_locks;
                g_table_locks = entry;
            }
        }
    }
    pthread_mutex_unlock(&g_table_map_mutex);

    return entry ? &entry->lock : &g_fallback_lock;
}

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

static void statement_access_clear(StatementAccess *access) {
    if (!access) {
        return;
    }
    free(access->table_name);
    memset(access, 0, sizeof *access);
}

static int statement_access_prepare(const char *sql, StatementAccess *out) {
    Lexer lex;
    InsertStmt *insert_stmt = NULL;
    SelectStmt *select_stmt = NULL;
    size_t len = 0;
    size_t start = 0;

    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->statement_type = SQL_STATEMENT_UNKNOWN;
    out->access_mode = ACCESS_WRITE;

    if (!sql) {
        return 0;
    }

    len = strlen(sql);
    start = skip_utf8_bom(sql, len);

    lexer_init(&lex, sql + start, len - start);
    if (parser_parse_insert(&lex, &insert_stmt) == 0 && insert_stmt != NULL) {
        out->statement_type = SQL_STATEMENT_INSERT;
        out->access_mode = ACCESS_WRITE;
        out->table_name = dup_cstr(insert_stmt->table);
        ast_insert_stmt_free(insert_stmt);
        return 0;
    }

    lexer_init(&lex, sql + start, len - start);
    if (parser_parse_select(&lex, &select_stmt) == 0 && select_stmt != NULL) {
        out->statement_type = SQL_STATEMENT_SELECT;
        out->access_mode = ACCESS_READ;
        out->table_name = dup_cstr(select_stmt->table);
        ast_select_stmt_free(select_stmt);
        return 0;
    }

    out->statement_type = detect_statement_type(sql);
    out->access_mode = (out->statement_type == SQL_STATEMENT_SELECT) ? ACCESS_READ : ACCESS_WRITE;
    return 0;
}

static void access_lock(FairRwLock *lock, AccessMode mode) {
    if (mode == ACCESS_READ) {
        fair_rwlock_rdlock(lock);
    } else {
        fair_rwlock_wrlock(lock);
    }
}

static void access_unlock(FairRwLock *lock, AccessMode mode) {
    if (mode == ACCESS_READ) {
        fair_rwlock_rdunlock(lock);
    } else {
        fair_rwlock_wrunlock(lock);
    }
}

int engine_adapter_execute_sql_with_stats(const char *sql, SqlExecutionResult *out, EngineExecutionStats *stats) {
    StatementAccess access = {0};
    FairRwLock *lock = NULL;
    int rc = 0;
    int64_t wait_start_ns = 0;
    int64_t exec_start_ns = 0;

    if (stats) {
        memset(stats, 0, sizeof *stats);
    }

    statement_access_prepare(sql, &access);
    lock = get_table_lock(access.table_name);

    wait_start_ns = monotonic_now_ns();
    access_lock(lock, access.access_mode);
    if (stats) {
        stats->lock_wait_ns = monotonic_now_ns() - wait_start_ns;
    }

    exec_start_ns = monotonic_now_ns();
    rc = sql_processor_run_text(sql, out, NULL);
    if (stats) {
        stats->execute_ns = monotonic_now_ns() - exec_start_ns;
    }

    access_unlock(lock, access.access_mode);
    statement_access_clear(&access);
    return rc;
}

int engine_adapter_execute_sql(const char *sql, SqlExecutionResult *out) {
    return engine_adapter_execute_sql_with_stats(sql, out, NULL);
}
