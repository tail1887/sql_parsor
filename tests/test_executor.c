#include "ast.h"
#include "csv_storage.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *m) {
    fprintf(stderr, "%s\n", m);
    return 1;
}

static int seed_exec_table(void) {
    FILE *fp = fopen("data/test_exec.csv", "wb");
    if (!fp) return -1;
    fputs("id,name,email\n", fp);
    fputs("1,alice,alice@example.com", fp);
    return fclose(fp);
}

static void cleanup_exec_table(void) {
    remove("data/test_exec.csv");
    remove("data/test_exec_out.txt");
}

static int parse_insert_sql(const char *sql, InsertStmt **st) {
    Lexer L;
    lexer_init(&L, sql, strlen(sql));
    return parser_parse_insert(&L, st);
}

static int parse_select_sql(const char *sql, SelectStmt **st) {
    Lexer L;
    lexer_init(&L, sql, strlen(sql));
    return parser_parse_select(&L, st);
}

static int test_insert_exec(void) {
    InsertStmt *st = NULL;
    if (parse_insert_sql("INSERT INTO test_exec VALUES (2, 'bob', NULL);", &st) != 0 || !st) {
        return fail("parse insert");
    }
    if (executor_execute_insert(st) != 0) {
        ast_insert_stmt_free(st);
        return fail("execute insert");
    }
    ast_insert_stmt_free(st);

    CsvTable *t = NULL;
    if (csv_storage_read_table("test_exec", &t) != 0 || !t) {
        return fail("read table after insert");
    }
    if (t->row_count != 2) {
        csv_storage_free_table(t);
        return fail("row_count");
    }
    if (strcmp(t->rows[1][0], "2") != 0 || strcmp(t->rows[1][1], "bob") != 0) {
        csv_storage_free_table(t);
        return fail("insert values");
    }
    if (strcmp(t->rows[1][2], "") != 0) {
        csv_storage_free_table(t);
        return fail("null as empty");
    }
    csv_storage_free_table(t);
    return 0;
}

static int test_select_exec_star(void) {
    SelectStmt *st = NULL;
    if (parse_select_sql("SELECT * FROM test_exec;", &st) != 0 || !st) {
        return fail("parse select star");
    }

    FILE *fp = fopen("data/test_exec_out.txt", "wb+");
    if (!fp) {
        ast_select_stmt_free(st);
        return fail("open out");
    }

    if (executor_execute_select(st, fp) != 0) {
        fclose(fp);
        ast_select_stmt_free(st);
        return fail("execute select star");
    }
    fflush(fp);
    fseek(fp, 0, SEEK_SET);

    char buf[1024] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    (void)n;
    fclose(fp);
    ast_select_stmt_free(st);

    if (strstr(buf, "id\tname\temail\n") == NULL) {
        return fail("star header");
    }
    if (strstr(buf, "1\talice\talice@example.com\n") == NULL) {
        return fail("star row1");
    }
    if (strstr(buf, "2\tbob\t\n") == NULL) {
        return fail("star row2");
    }
    return 0;
}

static int test_select_exec_columns(void) {
    SelectStmt *st = NULL;
    if (parse_select_sql("SELECT email, id FROM test_exec;", &st) != 0 || !st) {
        return fail("parse select columns");
    }

    FILE *fp = fopen("data/test_exec_out.txt", "wb+");
    if (!fp) {
        ast_select_stmt_free(st);
        return fail("open out2");
    }

    if (executor_execute_select(st, fp) != 0) {
        fclose(fp);
        ast_select_stmt_free(st);
        return fail("execute select cols");
    }
    fflush(fp);
    fseek(fp, 0, SEEK_SET);

    char buf[1024] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    (void)n;
    fclose(fp);
    ast_select_stmt_free(st);

    if (strstr(buf, "email\tid\n") == NULL) {
        return fail("cols header");
    }
    if (strstr(buf, "alice@example.com\t1\n") == NULL) {
        return fail("cols row1");
    }
    if (strstr(buf, "\t2\n") == NULL) {
        return fail("cols row2");
    }
    return 0;
}

int main(void) {
    cleanup_exec_table();
    if (seed_exec_table() != 0) return fail("seed table");

    if (test_insert_exec() != 0) {
        cleanup_exec_table();
        return 1;
    }
    if (test_select_exec_star() != 0) {
        cleanup_exec_table();
        return 1;
    }
    if (test_select_exec_columns() != 0) {
        cleanup_exec_table();
        return 1;
    }

    cleanup_exec_table();
    return 0;
}
