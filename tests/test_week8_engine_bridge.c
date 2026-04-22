#include "week8/engine_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *m) {
    fprintf(stderr, "%s\n", m);
    return 1;
}

static int write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    if (fwrite(content, 1, strlen(content), fp) != strlen(content)) {
        fclose(fp);
        return -1;
    }
    return fclose(fp);
}

static void cleanup_files(void) { remove("data/week8_bridge_users.csv"); }

int main(void) {
    cleanup_files();
    if (write_file("data/week8_bridge_users.csv", "id,name,email\n1,alice,alice@example.com\n") != 0) {
        return fail("seed table failed");
    }

    Week8EngineResult ok;
    if (week8_engine_bridge_execute_sql("SELECT id, email FROM week8_bridge_users;", &ok) != 0) {
        cleanup_files();
        return fail("bridge execute ok failed");
    }
    if (ok.status != WEEK8_ENGINE_OK || !ok.stdout_text || strstr(ok.stdout_text, "id\temail") == NULL) {
        week8_engine_bridge_result_free(&ok);
        cleanup_files();
        return fail("bridge ok mismatch");
    }
    week8_engine_bridge_result_free(&ok);

    Week8EngineResult parse;
    if (week8_engine_bridge_execute_sql("SELEC * FROM week8_bridge_users;", &parse) != 0) {
        cleanup_files();
        return fail("bridge execute parse failed");
    }
    if (parse.status != WEEK8_ENGINE_PARSE_ERR || !parse.stderr_text ||
        strstr(parse.stderr_text, "parse error") == NULL) {
        week8_engine_bridge_result_free(&parse);
        cleanup_files();
        return fail("bridge parse mismatch");
    }
    week8_engine_bridge_result_free(&parse);

    Week8EngineResult exec;
    if (week8_engine_bridge_execute_sql("SELECT * FROM week8_missing;", &exec) != 0) {
        cleanup_files();
        return fail("bridge execute exec failed");
    }
    if (exec.status != WEEK8_ENGINE_EXEC_ERR || !exec.stderr_text || strstr(exec.stderr_text, "exec error") == NULL) {
        week8_engine_bridge_result_free(&exec);
        cleanup_files();
        return fail("bridge exec mismatch");
    }
    week8_engine_bridge_result_free(&exec);

    cleanup_files();
    return 0;
}
