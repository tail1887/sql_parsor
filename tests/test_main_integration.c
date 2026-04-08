#include "sql_processor.h"

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

static int seed_table(void) {
    return write_file("data/test_main_users.csv", "id,name,email\n1,alice,alice@example.com");
}

static void cleanup_files(void) {
    remove("data/test_main_users.csv");
    remove("data/test_main_ok.sql");
    remove("data/test_main_parse.sql");
    remove("data/test_main_exec.sql");
    remove("data/test_main_out.txt");
    remove("data/test_main_err.txt");
}

static int run_case(const char *sql_path, int want_rc, const char *out_must, const char *err_must) {
    FILE *out = fopen("data/test_main_out.txt", "wb+");
    FILE *err = fopen("data/test_main_err.txt", "wb+");
    if (!out || !err) {
        if (out) fclose(out);
        if (err) fclose(err);
        return fail("open out/err");
    }

    int rc = sql_processor_run_file(sql_path, out, err);
    fflush(out);
    fflush(err);
    fseek(out, 0, SEEK_SET);
    fseek(err, 0, SEEK_SET);

    char ob[2048] = {0};
    char eb[2048] = {0};
    fread(ob, 1, sizeof(ob) - 1, out);
    fread(eb, 1, sizeof(eb) - 1, err);

    fclose(out);
    fclose(err);

    if (rc != want_rc) {
        fprintf(stderr, "rc mismatch: got %d want %d\n", rc, want_rc);
        return 1;
    }
    if (out_must && strstr(ob, out_must) == NULL) {
        return fail("stdout mismatch");
    }
    if (err_must && strstr(eb, err_must) == NULL) {
        return fail("stderr mismatch");
    }
    return 0;
}

int main(void) {
    cleanup_files();
    if (seed_table() != 0) return fail("seed table");

    if (write_file("data/test_main_ok.sql",
                   "INSERT INTO test_main_users VALUES (2, 'bob', NULL);\n"
                   "SELECT id, email FROM test_main_users;\n") != 0) {
        cleanup_files();
        return fail("write ok sql");
    }

    if (run_case("data/test_main_ok.sql", 0, "id\temail\n", NULL) != 0) {
        cleanup_files();
        return 1;
    }

    if (write_file("data/test_main_parse.sql", "INSER INTO test_main_users VALUES (3, 'x', NULL);\n") != 0) {
        cleanup_files();
        return fail("write parse sql");
    }
    if (run_case("data/test_main_parse.sql", 2, NULL, "parse error") != 0) {
        cleanup_files();
        return 1;
    }

    if (write_file("data/test_main_exec.sql", "INSERT INTO missing_table VALUES (1, 'x', NULL);\n") != 0) {
        cleanup_files();
        return fail("write exec sql");
    }
    if (run_case("data/test_main_exec.sql", 3, NULL, "exec error") != 0) {
        cleanup_files();
        return 1;
    }

    cleanup_files();
    return 0;
}
