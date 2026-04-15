#include "parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

static int ident_is_id(const Token *t) {
    if (!t || t->kind != TOKEN_IDENTIFIER || t->text_len != 2) {
        return 0;
    }
    char a = (char)toupper((unsigned char)t->text[0]);
    char b = (char)toupper((unsigned char)t->text[1]);
    return a == 'I' && b == 'D';
}

//문자열을 복사할 때 사용하는 함수.
static char *dup_slice(const char *s, size_t n) {
    char *p = malloc(n + 1); //문자열을 복사할 메모리를 할당한다.
    if (!p) {
        return NULL; //문자열을 복사할 수 없다면, NULL을 반환한다.
    }
    if (n > 0) {
        memcpy(p, s, n); //문자열을 복사한다.
    }
    p[n] = '\0'; //문자열의 끝에 널 문자를 추가한다.
    return p; //문자열을 복사한 메모리를 반환한다.
}

//다음 토큰이 원하는 토큰인지 확인하는 함수.
static int lexer_expect(Lexer *lex, TokenKind want, Token *tok) { //다음 토큰이 원하는 토큰인지 확인한다.
    if (lexer_next(lex, tok) != 0) {
        return -1;
    }
    if (tok->kind != want) {
        return -1;
    }
    return 0;
}

//토큰을 값으로 변환하는 함수.
static int value_from_token(const Token *t, SqlValue *out) {
    out->text = NULL;//값의 텍스트를 NULL로 초기화한다.
    if (t->kind == TOKEN_NULL) { //NULL 토큰이라면, 값의 종류를 NULL로 설정한다.
        out->kind = SQL_VALUE_NULL; //값의 종류를 NULL로 설정한다.
        return 0;
    }
    if (t->kind == TOKEN_INTEGER) { //정수 토큰이라면, 값의 종류를 정수로 설정한다.
        out->kind = SQL_VALUE_INT; //값의 종류를 정수로 설정한다.
        out->text = dup_slice(t->text, t->text_len);
        return out->text ? 0 : -1; //값의 텍스트를 복사한다. 만약 값의 텍스트를 복사할 수 없다면, 에러를 반환한다.
    }
    if (t->kind == TOKEN_STRING) { //문자열 토큰이라면, 값의 종류를 문자열로 설정한다.
        out->kind = SQL_VALUE_STRING;
        out->text = dup_slice(t->text, t->text_len); //값의 텍스트를 복사한다. 만약 값의 텍스트를 복사할 수 없다면, 에러를 반환한다.
        return out->text ? 0 : -1;
    }
    return -1; //값의 종류를 설정할 수 없다면, 에러를 반환한다.
}

//값을 해제하는 함수.
static void free_values_partial(SqlValue *vals, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(vals[i].text);
    }
    free(vals);
}

//컬럼 이름 배열의 일부를 해제하는 함수.
static void free_columns_partial(char **cols, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(cols[i]);
    }
    free(cols);
}

//INSERT INTO <table> VALUES (...); 에 대응하는 AST를 파싱하는 함수.
int parser_parse_insert(Lexer *lex, InsertStmt **out) {
    *out = NULL;
    Token t;

    if (lexer_expect(lex, TOKEN_INSERT, &t) != 0) { // INSERT 토큰을 예상한다.
        return -1;
    }
    if (lexer_expect(lex, TOKEN_INTO, &t) != 0) { // INTO 토큰을 예상한다.
        return -1;
    }
    if (lexer_next(lex, &t) != 0) { // 테이블 이름을 읽는다.
        return -1;
    }
    if (t.kind != TOKEN_IDENTIFIER) { // 테이블 이름이 식별자가 아니라면, 에러를 반환한다.
        return -1;
    }
    char *table = dup_slice(t.text, t.text_len); // 테이블 이름을 복사한다.
    if (!table) { // 테이블 이름을 복사할 수 없다면, 에러를 반환한다.
        return -1;
    }
    if (lexer_expect(lex, TOKEN_VALUES, &t) != 0) { // VALUES 토큰을 예상한다.
        free(table);
        return -1;
    }
    if (lexer_expect(lex, TOKEN_LPAREN, &t) != 0) { // ( 토큰을 예상한다.
        free(table);
        return -1;
    }

    size_t cap = 4; //값의 배열 초기 크기
    SqlValue *vals = calloc(cap, sizeof *vals); //값의 배열을 할당한다.
    if (!vals) { //값의 배열을 할당할 수 없다면, 에러를 반환한다.
        free(table);
        return -1;
    }
    size_t n = 0; //값의 개수를 0으로 초기화한다.

    if (lexer_next(lex, &t) != 0) { // 첫 값(또는 바로 ')')을 읽는다.
        free(table);
        free(vals);
        return -1;
    }
    if (t.kind == TOKEN_RPAREN) {
        /* 빈 값 목록 허용 */
    } else {
        if (value_from_token(&t, &vals[n]) != 0) { //토큰을 값으로 변환한다.
            free(table);
            free_values_partial(vals, n); //값의 배열을 해제한다.
            return -1;
        }
        n++;
        for (;;) { //값을 하나씩 읽어서 저장한다. 모든 값을 읽을 때까지 반복한다.
            if (lexer_next(lex, &t) != 0) { // 다음 토큰을 읽는다.
                free(table);
                free_values_partial(vals, n); //값의 배열을 해제한다.
                return -1;
            }
            if (t.kind == TOKEN_RPAREN) { // ) 토큰이라면, 반복문을 종료한다.
                break;
            }
            if (t.kind != TOKEN_COMMA) { // , 토큰이 아니라면, 에러를 반환한다.
                free(table); //테이블 이름을 해제한다.
                free_values_partial(vals, n); //값의 배열을 해제한다.
                return -1;
            }
            // , 토큰이라면, 다음 값을 읽는다.
            if (lexer_next(lex, &t) != 0) { // 다음 토큰을 읽는다.
                free(table); //테이블 이름을 해제한다.
                free_values_partial(vals, n); //값의 배열을 해제한다.
                return -1;
            }
            if (t.kind == TOKEN_RPAREN) { // ) 토큰이라면, 에러를 반환한다. ,뒤에 )가 올 수 없다.
                free(table); //테이블 이름을 해제한다.
                free_values_partial(vals, n); //값의 배열을 해제한다.
                return -1;
            }
            //값을 저장하기 전에, 값의 배열의 크기를 확인하고 늘린다.
            if (n >= cap) { //값의 개수가 값의 배열의 크기보다 크다면, 값의 배열의 크기를 2배로 늘린다.
                size_t ncap = cap * 2; //값의 배열의 크기를 2배로 늘린다.
                SqlValue *nv = realloc(vals, ncap * sizeof *nv); //값의 배열을 2배로 늘린다.
                if (!nv) { //값의 배열을 2배로 늘릴 수 없다면, 에러를 반환한다.
                    free(table); //테이블 이름을 해제한다.
                    free_values_partial(vals, n); //값의 배열을 해제한다.
                    return -1;
                }
                memset(nv + cap, 0, (ncap - cap) * sizeof *nv); //값의 배열의 나머지 부분을 0으로 초기화한다.
                vals = nv;
                cap = ncap; //값의 배열의 크기를 2배로 늘린다.
            }
            if (value_from_token(&t, &vals[n]) != 0) { //토큰을 값으로 변환한다.
                free(table); //테이블 이름을 해제한다.
                free_values_partial(vals, n); //값의 배열을 해제한다.
                return -1;
            }
            n++;
        }
    }

    if (lexer_next(lex, &t) != 0) { // 다음 토큰을 읽는다.
        free(table); //테이블 이름을 해제한다.
        free_values_partial(vals, n); //값의 배열을 해제한다.
        return -1;
    }
    if (t.kind != TOKEN_SEMICOLON && t.kind != TOKEN_EOF) { // ; 또는 EOF가 아니라면 에러.
        free(table); //테이블 이름을 해제한다.
        free_values_partial(vals, n); //값의 배열을 해제한다.
        return -1;
    }

    InsertStmt *stmt = malloc(sizeof *stmt); //InsertStmt를 할당한다.
    if (!stmt) { //InsertStmt를 할당할 수 없다면, 에러를 반환한다.
        free(table);
        free_values_partial(vals, n);
        return -1;
    }
    stmt->table = table; //테이블 이름을 저장한다.
    stmt->value_count = n; //값의 개수를 저장한다.
    if (n == 0) { //값의 개수가 0이라면, 값의 배열을 해제한다.
        free(vals); //값의 배열을 해제한다.
        stmt->values = NULL; //값의 배열을 NULL로 초기화한다.
    } else { //값의 개수가 0이 아니라면, 값의 배열을 축소한다.
        SqlValue *shrink = realloc(vals, n * sizeof *vals); //값의 배열을 축소한다.
        if (shrink) { //값의 배열을 축소할 수 있다면, 값의 배열을 축소한다.
            vals = shrink; //값의 배열을 축소한다.
        }
        stmt->values = vals; //값의 배열을 저장한다.
    }
    *out = stmt; //InsertStmt를 반환한다.
    return 0;
}

//SELECT * FROM <table> 또는 SELECT col,... FROM <table> 파싱.
int parser_parse_select(Lexer *lex, SelectStmt **out) {
    *out = NULL;
    Token t;

    if (lexer_expect(lex, TOKEN_SELECT, &t) != 0) {
        return -1;
    }

    int select_all = 0;
    char **cols = NULL;
    size_t ncols = 0;
    size_t cap = 0;

    if (lexer_next(lex, &t) != 0) {
        return -1;
    }

    if (t.kind == TOKEN_STAR) {
        select_all = 1;
    } else {
        // 첫 컬럼은 식별자여야 한다.
        if (t.kind != TOKEN_IDENTIFIER) {
            return -1;
        }
        cap = 4;
        cols = calloc(cap, sizeof *cols);
        if (!cols) {
            return -1;
        }
        cols[ncols] = dup_slice(t.text, t.text_len);
        if (!cols[ncols]) {
            free(cols);
            return -1;
        }
        ncols++;

        for (;;) {
            if (lexer_next(lex, &t) != 0) {
                free_columns_partial(cols, ncols);
                return -1;
            }
            if (t.kind != TOKEN_COMMA) {
                break; // FROM 확인 단계로 넘김
            }
            if (lexer_next(lex, &t) != 0) {
                free_columns_partial(cols, ncols);
                return -1;
            }
            if (t.kind != TOKEN_IDENTIFIER) {
                free_columns_partial(cols, ncols);
                return -1; // trailing comma 포함
            }
            if (ncols >= cap) {
                size_t ncap = cap * 2;
                char **nv = realloc(cols, ncap * sizeof *nv);
                if (!nv) {
                    free_columns_partial(cols, ncols);
                    return -1;
                }
                memset(nv + cap, 0, (ncap - cap) * sizeof *nv);
                cols = nv;
                cap = ncap;
            }
            cols[ncols] = dup_slice(t.text, t.text_len);
            if (!cols[ncols]) {
                free_columns_partial(cols, ncols);
                return -1;
            }
            ncols++;
        }
    }

    if (select_all) {
        // STAR 경로에서는 아직 FROM 토큰을 읽지 않았으므로 지금 읽는다.
        if (lexer_expect(lex, TOKEN_FROM, &t) != 0) {
            return -1;
        }
    } else {
        // 컬럼 루프에서 마지막으로 읽은 토큰 t 가 FROM 이어야 함.
        if (t.kind != TOKEN_FROM) {
            free_columns_partial(cols, ncols);
            return -1;
        }
    }

    if (lexer_next(lex, &t) != 0 || t.kind != TOKEN_IDENTIFIER) {
        free_columns_partial(cols, ncols);
        return -1;
    }
    char *table = dup_slice(t.text, t.text_len);
    if (!table) {
        free_columns_partial(cols, ncols);
        return -1;
    }

    if (lexer_next(lex, &t) != 0) {
        free(table);
        free_columns_partial(cols, ncols);
        return -1;
    }

    int has_where = 0;
    int64_t where_id = 0;
    if (t.kind == TOKEN_SEMICOLON || t.kind == TOKEN_EOF) {
        has_where = 0;
    } else if (t.kind == TOKEN_WHERE) {
        Token idtok;
        if (lexer_next(lex, &idtok) != 0 || !ident_is_id(&idtok)) {
            free(table);
            free_columns_partial(cols, ncols);
            return -1;
        }
        if (lexer_expect(lex, TOKEN_EQ, &t) != 0) {
            free(table);
            free_columns_partial(cols, ncols);
            return -1;
        }
        Token inttok;
        if (lexer_next(lex, &inttok) != 0 || inttok.kind != TOKEN_INTEGER) {
            free(table);
            free_columns_partial(cols, ncols);
            return -1;
        }
        char *tmp = dup_slice(inttok.text, inttok.text_len);
        if (!tmp) {
            free(table);
            free_columns_partial(cols, ncols);
            return -1;
        }
        where_id = (int64_t)strtoll(tmp, NULL, 10);
        free(tmp);
        has_where = 1;
        if (lexer_next(lex, &t) != 0) {
            free(table);
            free_columns_partial(cols, ncols);
            return -1;
        }
        if (t.kind != TOKEN_SEMICOLON && t.kind != TOKEN_EOF) {
            free(table);
            free_columns_partial(cols, ncols);
            return -1;
        }
    } else {
        free(table);
        free_columns_partial(cols, ncols);
        return -1;
    }

    SelectStmt *st = malloc(sizeof *st);
    if (!st) {
        free(table);
        free_columns_partial(cols, ncols);
        return -1;
    }
    st->has_where_id_eq = has_where;
    st->where_id_value = where_id;
    st->select_all = select_all;
    st->column_count = select_all ? 0 : ncols;
    if (select_all) {
        free(cols);
        st->columns = NULL;
    } else {
        char **shrink = realloc(cols, ncols * sizeof *cols);
        if (shrink) {
            cols = shrink;
        }
        st->columns = cols;
    }
    st->table = table;
    *out = st;
    return 0;
}
