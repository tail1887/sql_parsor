#ifndef ENGINE_ADAPTER_H
#define ENGINE_ADAPTER_H

#include "sql_result.h"

int engine_adapter_execute_sql(const char *sql, SqlExecutionResult *out);

#endif
