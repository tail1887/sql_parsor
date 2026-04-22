#include "engine_adapter.h"

#include "sql_processor.h"

#include <pthread.h>

static pthread_mutex_t g_engine_lock = PTHREAD_MUTEX_INITIALIZER;

int engine_adapter_execute_sql(const char *sql, SqlExecutionResult *out) {
    int rc = 0;

    pthread_mutex_lock(&g_engine_lock);
    rc = sql_processor_run_text(sql, out, NULL);
    pthread_mutex_unlock(&g_engine_lock);

    return rc;
}
