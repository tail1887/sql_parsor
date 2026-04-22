#include "csv_storage.h"
#include "sql_processor.h"
#include "week7/week7_index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

static int write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "wb");
    size_t want = strlen(content);
    if (!fp) {
        return -1;
    }
    if (fwrite(content, 1, want, fp) != want) {
        fclose(fp);
        return -1;
    }
    return fclose(fp);
}

static void cleanup_files(void) {
    week7_reset();
    remove("data/test_api_engine.csv");
}

static int seed_table(void) {
    week7_reset();
    return write_file("data/test_api_engine.csv", "id,name,email\n1,alice,alice@example.com\n");
}

static int test_select_result(void) {
    SqlExecutionResult result = {0};
    int rc = sql_processor_run_text("SELECT id, email FROM test_api_engine;", &result, NULL);
    if (rc != 0) {
        sql_processor_free_result(&result);
        return fail("select rc");
    }
    if (result.statement_type != SQL_STATEMENT_SELECT || result.exit_code != 0) {
        sql_processor_free_result(&result);
        return fail("select type");
    }
    if (!result.message || strcmp(result.message, "1 row selected") != 0) {
        sql_processor_free_result(&result);
        return fail("select message");
    }
    if (result.column_count != 2 || result.row_count != 1) {
        sql_processor_free_result(&result);
        return fail("select counts");
    }
    if (strcmp(result.columns[0], "id") != 0 || strcmp(result.columns[1], "email") != 0) {
        sql_processor_free_result(&result);
        return fail("select columns");
    }
    if (strcmp(result.rows[0][0], "1") != 0 || strcmp(result.rows[0][1], "alice@example.com") != 0) {
        sql_processor_free_result(&result);
        return fail("select row");
    }
    sql_processor_free_result(&result);
    return 0;
}

static int test_insert_result(void) {
    SqlExecutionResult result = {0};
    CsvTable *table = NULL;
    int rc = sql_processor_run_text("INSERT INTO test_api_engine VALUES ('bob', 'bob@example.com');", &result, NULL);
    if (rc != 0) {
        sql_processor_free_result(&result);
        return fail("insert rc");
    }
    if (result.statement_type != SQL_STATEMENT_INSERT || result.affected_rows != 1) {
        sql_processor_free_result(&result);
        return fail("insert result");
    }
    if (!result.message || strcmp(result.message, "1 row inserted") != 0) {
        sql_processor_free_result(&result);
        return fail("insert message");
    }
    sql_processor_free_result(&result);

    if (csv_storage_read_table("test_api_engine", &table) != 0 || !table) {
        return fail("insert read");
    }
    if (table->row_count != 2 || strcmp(table->rows[1][1], "bob") != 0 ||
        strcmp(table->rows[1][2], "bob@example.com") != 0) {
        csv_storage_free_table(table);
        return fail("insert stored row");
    }
    csv_storage_free_table(table);
    return 0;
}

static int test_multiple_statement_rejected(void) {
    SqlExecutionResult result = {0};
    int rc = sql_processor_run_text("SELECT * FROM test_api_engine; SELECT id FROM test_api_engine;", &result, NULL);
    if (rc != 2 || result.exit_code != 2) {
        sql_processor_free_result(&result);
        return fail("multi rc");
    }
    if (!result.message || strcmp(result.message, "parse error: exactly one statement required") != 0) {
        sql_processor_free_result(&result);
        return fail("multi message");
    }
    sql_processor_free_result(&result);
    return 0;
}

static int test_exec_error(void) {
    SqlExecutionResult result = {0};
    int rc = sql_processor_run_text("SELECT * FROM missing_api_table;", &result, NULL);
    if (rc != 3 || result.exit_code != 3 || result.statement_type != SQL_STATEMENT_SELECT) {
        sql_processor_free_result(&result);
        return fail("exec rc");
    }
    if (!result.message || strstr(result.message, "exec error") == NULL) {
        sql_processor_free_result(&result);
        return fail("exec message");
    }
    sql_processor_free_result(&result);
    return 0;
}

int main(void) {
    cleanup_files();
    if (seed_table() != 0) {
        cleanup_files();
        return fail("seed");
    }
    if (test_select_result() != 0) {
        cleanup_files();
        return 1;
    }
    if (test_insert_result() != 0) {
        cleanup_files();
        return 1;
    }
    if (test_multiple_statement_rejected() != 0) {
        cleanup_files();
        return 1;
    }
    if (test_exec_error() != 0) {
        cleanup_files();
        return 1;
    }
    cleanup_files();
    return 0;
}
