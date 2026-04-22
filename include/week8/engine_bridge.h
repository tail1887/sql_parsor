#ifndef WEEK8_ENGINE_BRIDGE_H
#define WEEK8_ENGINE_BRIDGE_H

#include <stddef.h>

typedef enum {
    WEEK8_ENGINE_OK = 0,
    WEEK8_ENGINE_CLI_USAGE_ERR = 1,
    WEEK8_ENGINE_PARSE_ERR = 2,
    WEEK8_ENGINE_EXEC_ERR = 3
} Week8EngineStatus;

typedef struct {
    Week8EngineStatus status;
    char *stdout_text;
    char *stderr_text;
} Week8EngineResult;

int week8_engine_bridge_execute_sql(const char *sql_text, Week8EngineResult *out_result);
void week8_engine_bridge_result_free(Week8EngineResult *result);

#endif
