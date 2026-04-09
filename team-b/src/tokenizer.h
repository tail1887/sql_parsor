#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

/*
 * TokenType은 Step 2에서 지원하는 최소 SQL 토큰 종류만 담는다.
 * 범용 SQL 토큰은 추가하지 않고, 이번 프로젝트에 필요한 것만 유지한다.
 */
typedef enum {
    TOKEN_SELECT,
    TOKEN_INSERT,
    TOKEN_INTO,
    TOKEN_VALUES,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_STAR,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_EQUAL,
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_STRING
} TokenType;

/*
 * Token 한 개는 "종류"와 "실제 문자열 값"을 가진다.
 * 예:
 *   SELECT  -> type = TOKEN_SELECT, text = "SELECT"
 *   302     -> type = TOKEN_INTEGER, text = "302"
 *   'Kim'   -> type = TOKEN_STRING, text = "Kim"
 *
 * STRING은 작은따옴표를 제외한 내부 내용만 text에 저장한다.
 */
typedef struct {
    TokenType type;
    char *text;
} Token;

/*
 * TokenList는 Token 여러 개를 담는 동적 배열이다.
 * items[0], items[1], ... 에 토큰이 순서대로 저장된다.
 */
typedef struct {
    Token *items;
    size_t count;
} TokenList;

int tokenize_sql(const char *sql_text, TokenList *out_tokens);
void free_token_list(TokenList *list);
const char *token_type_name(TokenType type);

#endif
