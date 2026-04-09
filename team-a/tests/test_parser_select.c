#include "ast.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *m) {
    fprintf(stderr, "%s\n", m);
    return 1;
}

static int test_select_star(void) {
    const char *s = "SELECT * FROM users;";
    Lexer L;
    lexer_init(&L, s, strlen(s));
    SelectStmt *st = NULL;
    if (parser_parse_select(&L, &st) != 0 || !st) return fail("parse star");
    if (!st->select_all) return fail("select_all");
    if (strcmp(st->table, "users") != 0) return fail("table");
    ast_select_stmt_free(st);
    return 0;
}

static int test_select_columns(void) {
    const char *s = "SELECT id, name FROM users";
    Lexer L;
    lexer_init(&L, s, strlen(s));
    SelectStmt *st = NULL;
    if (parser_parse_select(&L, &st) != 0 || !st) return fail("parse cols");
    if (st->select_all) return fail("not star");
    if (st->column_count != 2) return fail("count");
    if (strcmp(st->columns[0], "id") != 0 || strcmp(st->columns[1], "name") != 0) return fail("cols");
    ast_select_stmt_free(st);
    return 0;
}

static int test_fail_missing_from(void) {
    const char *s = "SELECT id users";
    Lexer L;
    lexer_init(&L, s, strlen(s));
    SelectStmt *st = NULL;
    if (parser_parse_select(&L, &st) == 0) {
        ast_select_stmt_free(st);
        return fail("missing from must fail");
    }
    return 0;
}

static int test_fail_trailing_comma(void) {
    const char *s = "SELECT id, FROM users";
    Lexer L;
    lexer_init(&L, s, strlen(s));
    SelectStmt *st = NULL;
    if (parser_parse_select(&L, &st) == 0) {
        ast_select_stmt_free(st);
        return fail("trailing comma must fail");
    }
    return 0;
}

int main(void) {
    struct {
        const char *n;
        int (*f)(void);
    } c[] = {
        {"select_star", test_select_star},
        {"select_columns", test_select_columns},
        {"fail_missing_from", test_fail_missing_from},
        {"fail_trailing_comma", test_fail_trailing_comma},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        if (c[i].f() != 0) {
            fprintf(stderr, "FAIL %s\n", c[i].n);
            return 1;
        }
    }
    return 0;
}
