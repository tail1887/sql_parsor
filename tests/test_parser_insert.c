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

static int test_ok_simple(void) {
    const char *s = "INSERT INTO users VALUES (1, 'a', NULL);";
    Lexer L;
    lexer_init(&L, s, strlen(s));
    InsertStmt *st = NULL;
    if (parser_parse_insert(&L, &st) != 0 || st == NULL) {
        return fail("parse failed");
    }
    if (strcmp(st->table, "users") != 0) {
        return fail("table");
    }
    if (st->value_count != 3) {
        return fail("value_count");
    }
    if (st->values[0].kind != SQL_VALUE_INT || strcmp(st->values[0].text, "1") != 0) {
        return fail("v0");
    }
    if (st->values[1].kind != SQL_VALUE_STRING || strcmp(st->values[1].text, "a") != 0) {
        return fail("v1");
    }
    if (st->values[2].kind != SQL_VALUE_NULL || st->values[2].text != NULL) {
        return fail("v2");
    }
    ast_insert_stmt_free(st);
    return 0;
}

static int test_ok_no_semicolon(void) {
    const char *s = "INSERT INTO t VALUES (-5, +10)";
    Lexer L;
    lexer_init(&L, s, strlen(s));
    InsertStmt *st = NULL;
    if (parser_parse_insert(&L, &st) != 0 || !st) {
        return fail("no semicolon parse");
    }
    if (strcmp(st->table, "t") != 0 || st->value_count != 2) {
        return fail("fields");
    }
    if (strcmp(st->values[0].text, "-5") != 0 || strcmp(st->values[1].text, "+10") != 0) {
        return fail("ints");
    }
    ast_insert_stmt_free(st);
    return 0;
}

static int test_fail_wrong_kw(void) {
    const char *s = "INSERT SELECT 1";
    Lexer L;
    lexer_init(&L, s, strlen(s));
    InsertStmt *st = NULL;
    if (parser_parse_insert(&L, &st) == 0) {
        ast_insert_stmt_free(st);
        return fail("expected fail");
    }
    if (st != NULL) {
        return fail("out should be null");
    }
    return 0;
}

static int test_fail_unclosed_paren(void) {
    const char *s = "INSERT INTO t VALUES (1";
    Lexer L;
    lexer_init(&L, s, strlen(s));
    InsertStmt *st = NULL;
    if (parser_parse_insert(&L, &st) == 0) {
        ast_insert_stmt_free(st);
        return fail("expected fail unclosed");
    }
    return 0;
}

int main(void) {
    struct {
        const char *n;
        int (*f)(void);
    } c[] = {
        {"ok_simple", test_ok_simple},
        {"ok_no_semicolon", test_ok_no_semicolon},
        {"fail_wrong_kw", test_fail_wrong_kw},
        {"fail_unclosed_paren", test_fail_unclosed_paren},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        if (c[i].f() != 0) {
            fprintf(stderr, "FAIL %s\n", c[i].n);
            return 1;
        }
    }
    return 0;
}
