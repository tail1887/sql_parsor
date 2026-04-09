#include "tokenizer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*
 * Step 2 tokenizer는 SQL 한 문장을 문자 단위로 읽어서 토큰 배열로 바꾼다.
 * 아직 SQL 문법이 맞는지 해석하지는 않고, "어떤 토큰들이 있는지"만 분리한다.
 */

static void initialize_token_list(TokenList *list)
{
    list->items = NULL;
    list->count = 0U;
}

static int ensure_token_capacity(TokenList *list, size_t *capacity)
{
    Token *new_items;
    size_t new_capacity;

    /*
     * TokenList도 StatementList와 같은 방식으로 동적 배열을 사용한다.
     * 칸이 부족하면 처음에는 8칸, 이후에는 2배씩 늘린다.
     */
    if (list->count < *capacity) {
        return 0;
    }

    new_capacity = (*capacity == 0U) ? 8U : (*capacity * 2U);
    new_items = (Token *)realloc(list->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        return 1;
    }

    list->items = new_items;
    *capacity = new_capacity;
    return 0;
}

static int append_token(
    TokenList *list,
    size_t *capacity,
    TokenType type,
    const char *start,
    size_t length)
{
    char *token_text;

    if (ensure_token_capacity(list, capacity) != 0) {
        return 1;
    }

    /*
     * 토큰 문자열도 따로 복사해 둔다.
     * 이후 parser 단계에서 원문 버퍼와 독립적으로 안전하게 사용할 수 있게 하기 위함이다.
     */
    token_text = (char *)malloc(length + 1U);
    if (token_text == NULL) {
        return 1;
    }

    memcpy(token_text, start, length);
    token_text[length] = '\0';

    list->items[list->count].type = type;
    list->items[list->count].text = token_text;
    list->count += 1U;
    return 0;
}

static int is_identifier_start_char(char c)
{
    unsigned char current;

    current = (unsigned char)c;
    return isalpha(current) || c == '_';
}

static int is_identifier_char(char c)
{
    unsigned char current;

    current = (unsigned char)c;
    return isalnum(current) || c == '_';
}

static TokenType token_type_for_identifier(const char *start, size_t length)
{
    /*
     * 키워드는 명세 예시처럼 대문자 입력만 지원한다.
     * 일치하지 않으면 일반 식별자(IDENTIFIER)로 처리한다.
     */
    if (length == 6U && strncmp(start, "SELECT", 6U) == 0) {
        return TOKEN_SELECT;
    }

    if (length == 6U && strncmp(start, "INSERT", 6U) == 0) {
        return TOKEN_INSERT;
    }

    if (length == 4U && strncmp(start, "INTO", 4U) == 0) {
        return TOKEN_INTO;
    }

    if (length == 6U && strncmp(start, "VALUES", 6U) == 0) {
        return TOKEN_VALUES;
    }

    if (length == 4U && strncmp(start, "FROM", 4U) == 0) {
        return TOKEN_FROM;
    }

    if (length == 5U && strncmp(start, "WHERE", 5U) == 0) {
        return TOKEN_WHERE;
    }

    return TOKEN_IDENTIFIER;
}

void free_token_list(TokenList *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }

    /*
     * 각 토큰의 text는 malloc으로 따로 만들었으므로 하나씩 free해야 한다.
     * 마지막에 Token 배열 자체도 해제한다.
     */
    for (i = 0; i < list->count; i++) {
        free(list->items[i].text);
    }

    free(list->items);
    list->items = NULL;
    list->count = 0U;
}

const char *token_type_name(TokenType type)
{
    switch (type) {
    case TOKEN_SELECT:
        return "TOKEN_SELECT";
    case TOKEN_INSERT:
        return "TOKEN_INSERT";
    case TOKEN_INTO:
        return "TOKEN_INTO";
    case TOKEN_VALUES:
        return "TOKEN_VALUES";
    case TOKEN_FROM:
        return "TOKEN_FROM";
    case TOKEN_WHERE:
        return "TOKEN_WHERE";
    case TOKEN_STAR:
        return "TOKEN_STAR";
    case TOKEN_LPAREN:
        return "TOKEN_LPAREN";
    case TOKEN_RPAREN:
        return "TOKEN_RPAREN";
    case TOKEN_COMMA:
        return "TOKEN_COMMA";
    case TOKEN_SEMICOLON:
        return "TOKEN_SEMICOLON";
    case TOKEN_EQUAL:
        return "TOKEN_EQUAL";
    case TOKEN_IDENTIFIER:
        return "TOKEN_IDENTIFIER";
    case TOKEN_INTEGER:
        return "TOKEN_INTEGER";
    case TOKEN_STRING:
        return "TOKEN_STRING";
    default:
        return "TOKEN_UNKNOWN";
    }
}

int tokenize_sql(const char *sql_text, TokenList *out_tokens)
{
    TokenList tokens;
    size_t capacity;
    size_t i;

    /*
     * sql_text는 SQL 한 문장 전체 문자열이다.
     * out_tokens는 이 함수가 만든 토큰 배열을 함수 밖으로 돌려주기 위한 출력 포인터다.
     */
    if (sql_text == NULL || out_tokens == NULL) {
        return 1;
    }

    *out_tokens = (TokenList){NULL, 0U};
    initialize_token_list(&tokens);
    capacity = 0U;
    i = 0U;

    /*
     * 문자열을 왼쪽에서 오른쪽으로 한 글자씩 훑는다.
     * 공백은 건너뛰고, 보이는 문자 종류에 따라 토큰을 하나씩 만든다.
     */
    while (sql_text[i] != '\0') {
        unsigned char current;

        current = (unsigned char)sql_text[i];

        if (isspace(current)) {
            i += 1U;
            continue;
        }

        /*
         * 기호 토큰은 현재 문자 하나만 보면 바로 결정할 수 있다.
         * 예: *, (, ), ,, ;, =
         */
        if (sql_text[i] == '*') {
            if (append_token(&tokens, &capacity, TOKEN_STAR, sql_text + i, 1U) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            i += 1U;
            continue;
        }

        if (sql_text[i] == '(') {
            if (append_token(&tokens, &capacity, TOKEN_LPAREN, sql_text + i, 1U) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            i += 1U;
            continue;
        }

        if (sql_text[i] == ')') {
            if (append_token(&tokens, &capacity, TOKEN_RPAREN, sql_text + i, 1U) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            i += 1U;
            continue;
        }

        if (sql_text[i] == ',') {
            if (append_token(&tokens, &capacity, TOKEN_COMMA, sql_text + i, 1U) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            i += 1U;
            continue;
        }

        if (sql_text[i] == ';') {
            if (append_token(&tokens, &capacity, TOKEN_SEMICOLON, sql_text + i, 1U) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            i += 1U;
            continue;
        }

        if (sql_text[i] == '=') {
            if (append_token(&tokens, &capacity, TOKEN_EQUAL, sql_text + i, 1U) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            i += 1U;
            continue;
        }

        /*
         * 작은따옴표 문자열은 시작 따옴표를 만나면
         * 다음 작은따옴표가 나올 때까지 전부 STRING으로 읽는다.
         * STRING 토큰의 text에는 따옴표를 제외한 내부 내용만 저장한다.
         */
        if (sql_text[i] == '\'') {
            size_t string_start;

            string_start = i + 1U;
            i += 1U;

            while (sql_text[i] != '\0' && sql_text[i] != '\'') {
                i += 1U;
            }

            if (sql_text[i] != '\'') {
                free_token_list(&tokens);
                return 1;
            }

            if (append_token(
                    &tokens,
                    &capacity,
                    TOKEN_STRING,
                    sql_text + string_start,
                    i - string_start) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            i += 1U;
            continue;
        }

        /*
         * 숫자로 시작하면 연속된 숫자들을 하나의 INTEGER 토큰으로 읽는다.
         * 이번 명세에서는 정수만 있으면 충분하므로 소수점이나 부호는 처리하지 않는다.
         */
        if (isdigit(current)) {
            size_t integer_start;

            integer_start = i;
            while (isdigit((unsigned char)sql_text[i])) {
                i += 1U;
            }

            if (append_token(
                    &tokens,
                    &capacity,
                    TOKEN_INTEGER,
                    sql_text + integer_start,
                    i - integer_start) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            continue;
        }

        /*
         * 알파벳 또는 '_'로 시작하면 식별자나 키워드 후보로 본다.
         * 먼저 끝까지 읽은 뒤, SELECT / INSERT 같은 예약어인지 확인한다.
         */
        if (is_identifier_start_char(sql_text[i])) {
            size_t identifier_start;
            TokenType type;

            identifier_start = i;
            while (is_identifier_char(sql_text[i])) {
                i += 1U;
            }

            type = token_type_for_identifier(sql_text + identifier_start, i - identifier_start);
            if (append_token(
                    &tokens,
                    &capacity,
                    type,
                    sql_text + identifier_start,
                    i - identifier_start) != 0) {
                free_token_list(&tokens);
                return 1;
            }

            continue;
        }

        /*
         * 여기까지 왔다는 것은 Step 2 명세에 없는 문자를 만났다는 뜻이다.
         * 예: #, >, / 같은 문자는 아직 지원하지 않는다.
         */
        free_token_list(&tokens);
        return 1;
    }

    *out_tokens = tokens;
    return 0;
}
