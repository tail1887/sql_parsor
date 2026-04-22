#include "week8/engine_bridge.h"

#include "sql_processor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_or_empty(char *s) {
    if (!s) {
        char *empty = (char *)calloc(1, 1);
        return empty;
    }
    return s;
}

int week8_engine_bridge_execute_sql(const char *sql_text, Week8EngineResult *out_result) {
    if (!out_result) return -1;
    memset(out_result, 0, sizeof(*out_result));

    char *out_buf = NULL;
    char *err_buf = NULL;
    size_t out_len = 0;
    size_t err_len = 0;

    FILE *out = open_memstream(&out_buf, &out_len);
    FILE *err = open_memstream(&err_buf, &err_len);
    if (!out || !err) {
        if (out) fclose(out);
        if (err) fclose(err);
        free(out_buf);
        free(err_buf);
        return -1;
    }

    int rc = sql_processor_run_text(sql_text, out, err);
    fflush(out);
    fflush(err);
    fclose(out);
    fclose(err);

    out_result->status = (Week8EngineStatus)rc;
    out_result->stdout_text = dup_or_empty(out_buf);
    out_result->stderr_text = dup_or_empty(err_buf);
    if (!out_result->stdout_text || !out_result->stderr_text) {
        week8_engine_bridge_result_free(out_result);
        return -1;
    }
    return 0;
}

void week8_engine_bridge_result_free(Week8EngineResult *result) {
    if (!result) return;
    free(result->stdout_text);
    free(result->stderr_text);
    result->stdout_text = NULL;
    result->stderr_text = NULL;
}
