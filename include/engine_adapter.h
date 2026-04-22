#ifndef ENGINE_ADAPTER_H
#define ENGINE_ADAPTER_H

#include "sql_result.h"

#include <stdint.h>

typedef struct {
    int64_t lock_wait_ns;
    int64_t execute_ns;
} EngineExecutionStats;

int engine_adapter_execute_sql(const char *sql, SqlExecutionResult *out);
int engine_adapter_execute_sql_with_stats(const char *sql, SqlExecutionResult *out, EngineExecutionStats *stats);

#endif
