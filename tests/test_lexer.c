#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    return 1;
}

static int lexer_next_ok(Lexer *lex, Token *tok) {
    if (lexer_next(lex, tok) != 0) {
        return -1;
    }
    return 0;
}

static int expect_kind(Lexer *lex, TokenKind want) {
    Token tok;
    if (lexer_next_ok(lex, &tok) != 0) {
        return fail_msg("lexer_next failed");
    }
    if (tok.kind != want) {
        fprintf(stderr, "expected kind %d got %d\n", (int)want, (int)tok.kind);
        return 1;
    }
    return 0;
}

static int expect_ident(Lexer *lex, const char *want) {
    Token tok;
    if (lexer_next_ok(lex, &tok) != 0) {
        return fail_msg("lexer_next failed");
    }
    if (tok.kind != TOKEN_IDENTIFIER) {
        return fail_msg("expected IDENTIFIER");
    }
    size_t wlen = strlen(want);
    if (tok.text_len != wlen || memcmp(tok.text, want, wlen) != 0) {
        return fail_msg("identifier text mismatch");
    }
    return 0;
}

static int expect_string(Lexer *lex, const char *decoded) {
    Token tok;
    if (lexer_next_ok(lex, &tok) != 0) {
        return fail_msg("lexer_next failed");
    }
    if (tok.kind != TOKEN_STRING) {
        return fail_msg("expected STRING");
    }
    size_t wlen = strlen(decoded);
    if (tok.text_len != wlen || memcmp(tok.text, decoded, wlen) != 0) {
        return fail_msg("string content mismatch");
    }
    return 0;
}

static int expect_integer_text(Lexer *lex, const char *want) {
    Token tok;
    if (lexer_next_ok(lex, &tok) != 0) {
        return fail_msg("lexer_next failed");
    }
    if (tok.kind != TOKEN_INTEGER) {
        return fail_msg("expected INTEGER");
    }
    size_t wlen = strlen(want);
    if (tok.text_len != wlen || memcmp(tok.text, want, wlen) != 0) {
        return fail_msg("integer text mismatch");
    }
    return 0;
}

static int expect_eof(Lexer *lex) {
    Token tok;
    if (lexer_next_ok(lex, &tok) != 0) {
        return fail_msg("lexer_next failed");
    }
    if (tok.kind != TOKEN_EOF) {
        return fail_msg("expected EOF");
    }
    return 0;
}

static int test_insert_case_insensitive(void) {
    const char *s = "INSerT InTo users";
    Lexer lex;
    lexer_init(&lex, s, strlen(s));
    if (expect_kind(&lex, TOKEN_INSERT) != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_INTO) != 0) {
        return 1;
    }
    if (expect_ident(&lex, "users") != 0) {
        return 1;
    }
    return expect_eof(&lex);
}

static int test_comment_select_line(void) {
    const char *s = "-- c\nSELECT * FROM t1;";
    Lexer lex;
    lexer_init(&lex, s, strlen(s));
    if (expect_kind(&lex, TOKEN_SELECT) != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_STAR) != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_FROM) != 0) {
        return 1;
    }
    if (expect_ident(&lex, "t1") != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_SEMICOLON) != 0) {
        return 1;
    }
    return expect_eof(&lex);
}

static int test_string_escape(void) {
    const char *s = "'a''b'";
    Lexer lex;
    lexer_init(&lex, s, strlen(s));
    if (expect_string(&lex, "a'b") != 0) {
        return 1;
    }
    return expect_eof(&lex);
}

static int test_values_integers_and_null(void) {
    const char *s = "VALUES ( +42 , -1 , NULL )";
    Lexer lex;
    lexer_init(&lex, s, strlen(s));
    if (expect_kind(&lex, TOKEN_VALUES) != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_LPAREN) != 0) {
        return 1;
    }
    if (expect_integer_text(&lex, "+42") != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_COMMA) != 0) {
        return 1;
    }
    if (expect_integer_text(&lex, "-1") != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_COMMA) != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_NULL) != 0) {
        return 1;
    }
    if (expect_kind(&lex, TOKEN_RPAREN) != 0) {
        return 1;
    }
    return expect_eof(&lex);
}

static int test_unterminated_string(void) {
    const char *s = "'no closing";
    Lexer lex;
    lexer_init(&lex, s, strlen(s));
    Token tok;
    if (lexer_next(&lex, &tok) != -1) {
        return fail_msg("expected lexer_next == -1 for unterminated string");
    }
    if (tok.kind != TOKEN_ERROR) {
        return fail_msg("expected TOKEN_ERROR");
    }
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } cases[] = {
        {"insert_case_insensitive", test_insert_case_insensitive},
        {"comment_select_line", test_comment_select_line},
        {"string_escape", test_string_escape},
        {"values_integers_and_null", test_values_integers_and_null},
        {"unterminated_string", test_unterminated_string},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int rc = cases[i].fn();
        if (rc != 0) {
            fprintf(stderr, "FAIL %s\n", cases[i].name);
            return 1;
        }
    }
    return 0;
}
