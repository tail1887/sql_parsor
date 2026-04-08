#include "parser.h"

#include <stdlib.h>
#include <string.h>

static char *dup_slice(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) {
        return NULL;
    }
    if (n > 0) {
        memcpy(p, s, n);
    }
    p[n] = '\0';
    return p;
}

static int lexer_expect(Lexer *lex, TokenKind want, Token *tok) {
    if (lexer_next(lex, tok) != 0) {
        return -1;
    }
    if (tok->kind != want) {
        return -1;
    }
    return 0;
}

static int value_from_token(const Token *t, SqlValue *out) {
    out->text = NULL;
    if (t->kind == TOKEN_NULL) {
        out->kind = SQL_VALUE_NULL;
        return 0;
    }
    if (t->kind == TOKEN_INTEGER) {
        out->kind = SQL_VALUE_INT;
        out->text = dup_slice(t->text, t->text_len);
        return out->text ? 0 : -1;
    }
    if (t->kind == TOKEN_STRING) {
        out->kind = SQL_VALUE_STRING;
        out->text = dup_slice(t->text, t->text_len);
        return out->text ? 0 : -1;
    }
    return -1;
}

static void free_values_partial(SqlValue *vals, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(vals[i].text);
    }
    free(vals);
}

int parser_parse_insert(Lexer *lex, InsertStmt **out) {
    *out = NULL;
    Token t;

    if (lexer_expect(lex, TOKEN_INSERT, &t) != 0) {
        return -1;
    }
    if (lexer_expect(lex, TOKEN_INTO, &t) != 0) {
        return -1;
    }
    if (lexer_next(lex, &t) != 0) {
        return -1;
    }
    if (t.kind != TOKEN_IDENTIFIER) {
        return -1;
    }
    char *table = dup_slice(t.text, t.text_len);
    if (!table) {
        return -1;
    }
    if (lexer_expect(lex, TOKEN_VALUES, &t) != 0) {
        free(table);
        return -1;
    }
    if (lexer_expect(lex, TOKEN_LPAREN, &t) != 0) {
        free(table);
        return -1;
    }

    size_t cap = 4;
    SqlValue *vals = calloc(cap, sizeof *vals);
    if (!vals) {
        free(table);
        return -1;
    }
    size_t n = 0;

    if (lexer_next(lex, &t) != 0) {
        free(table);
        free(vals);
        return -1;
    }
    if (t.kind == TOKEN_RPAREN) {
        /* 빈 값 목록 허용 */
    } else {
        if (value_from_token(&t, &vals[n]) != 0) {
            free(table);
            free_values_partial(vals, n);
            return -1;
        }
        n++;
        for (;;) {
            if (lexer_next(lex, &t) != 0) {
                free(table);
                free_values_partial(vals, n);
                return -1;
            }
            if (t.kind == TOKEN_RPAREN) {
                break;
            }
            if (t.kind != TOKEN_COMMA) {
                free(table);
                free_values_partial(vals, n);
                return -1;
            }
            if (lexer_next(lex, &t) != 0) {
                free(table);
                free_values_partial(vals, n);
                return -1;
            }
            if (t.kind == TOKEN_RPAREN) {
                free(table);
                free_values_partial(vals, n);
                return -1;
            }
            if (n >= cap) {
                size_t ncap = cap * 2;
                SqlValue *nv = realloc(vals, ncap * sizeof *nv);
                if (!nv) {
                    free(table);
                    free_values_partial(vals, n);
                    return -1;
                }
                memset(nv + cap, 0, (ncap - cap) * sizeof *nv);
                vals = nv;
                cap = ncap;
            }
            if (value_from_token(&t, &vals[n]) != 0) {
                free(table);
                free_values_partial(vals, n);
                return -1;
            }
            n++;
        }
    }

    if (lexer_next(lex, &t) != 0) {
        free(table);
        free_values_partial(vals, n);
        return -1;
    }
    if (t.kind != TOKEN_SEMICOLON && t.kind != TOKEN_EOF) {
        free(table);
        free_values_partial(vals, n);
        return -1;
    }

    InsertStmt *stmt = malloc(sizeof *stmt);
    if (!stmt) {
        free(table);
        free_values_partial(vals, n);
        return -1;
    }
    stmt->table = table;
    stmt->value_count = n;
    if (n == 0) {
        free(vals);
        stmt->values = NULL;
    } else {
        SqlValue *shrink = realloc(vals, n * sizeof *vals);
        if (shrink) {
            vals = shrink;
        }
        stmt->values = vals;
    }
    *out = stmt;
    return 0;
}
