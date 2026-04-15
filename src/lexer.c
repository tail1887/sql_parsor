/**
 * lexer.c — SQL 부분 문자열을 Token 스트림으로 바꾸는 구현.
 * 규칙은 docs/03-api-reference.md (공백, -- 주석, 식별자, 작은따옴표 문자열, 키워드 대소문자 무시 등) 를 따른다.
 */

#include "lexer.h"

#include <ctype.h>
#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* 식별자·키워드 보조 함수 (ASCII만; UTF-8 다바이트 식별자는 MVP 범위 밖)     */
/* -------------------------------------------------------------------------- */

/** 식별자 첫 글자: 영문 대소문자 또는 밑줄 (docs: ASCII로 시작). */
static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/** 식별자 후속 글자: 첫 글자 규칙 또는 숫자. */
static int is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

/** 키워드 비교용 ASCII 대문자 → 소문자 한 글자 (표준 toupper 의 subset). */
static char to_lower_c(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

/**
 * 길이 n 인 토큰 문자열 s 가 예약어와 일치하는지(대소문자 무시) 검사.
 * 일치하면 *out 에 해당 TokenKind 를 넣고 1 반환, 아니면 0 반환.
 * (strncmp+tolower 대신 분기: libc 의 비표준 strcasecmp 의존 회피)
 */
static int keyword_kind(const char *s, size_t n, TokenKind *out) {
    if (n == 6) {
        //INSERT 키워드 검사
        if (to_lower_c(s[0]) == 'i' && to_lower_c(s[1]) == 'n' && to_lower_c(s[2]) == 's' &&
            to_lower_c(s[3]) == 'e' && to_lower_c(s[4]) == 'r' && to_lower_c(s[5]) == 't') {
            *out = TOKEN_INSERT;
            return 1;
        }
        //SELECT 키워드 검사
        if (to_lower_c(s[0]) == 's' && to_lower_c(s[1]) == 'e' && to_lower_c(s[2]) == 'l' &&
            to_lower_c(s[3]) == 'e' && to_lower_c(s[4]) == 'c' && to_lower_c(s[5]) == 't') {
            *out = TOKEN_SELECT;
            return 1;
        }
        //VALUES 키워드 검사
        if (to_lower_c(s[0]) == 'v' && to_lower_c(s[1]) == 'a' && to_lower_c(s[2]) == 'l' &&
            to_lower_c(s[3]) == 'u' && to_lower_c(s[4]) == 'e' && to_lower_c(s[5]) == 's') {
            *out = TOKEN_VALUES;
            return 1;
        }
    }
    if (n == 5) {
        if (to_lower_c(s[0]) == 'w' && to_lower_c(s[1]) == 'h' && to_lower_c(s[2]) == 'e' &&
            to_lower_c(s[3]) == 'r' && to_lower_c(s[4]) == 'e') {
            *out = TOKEN_WHERE;
            return 1;
        }
    }
    if (n == 4) {
        //INTO 키워드 검사
        if (to_lower_c(s[0]) == 'i' && to_lower_c(s[1]) == 'n' && to_lower_c(s[2]) == 't' &&
            to_lower_c(s[3]) == 'o') {
            *out = TOKEN_INTO;
            return 1;
        }
        //NULL 키워드 검사
        if (to_lower_c(s[0]) == 'n' && to_lower_c(s[1]) == 'u' && to_lower_c(s[2]) == 'l' &&
            to_lower_c(s[3]) == 'l') {
            *out = TOKEN_NULL;
            return 1;
        }
        //FROM 키워드 검사
        if (to_lower_c(s[0]) == 'f' && to_lower_c(s[1]) == 'r' && to_lower_c(s[2]) == 'o' &&
            to_lower_c(s[3]) == 'm') {
            *out = TOKEN_FROM;
            return 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* 공백·주석 스킵 — 토큰으로 내보내지 않음                                     */
/* -------------------------------------------------------------------------- */

/**
 * pos 에서부터 연속된 공백과 '--' 줄 주석을 소비한다.
 * - 공백/탭/CR: column 만 증가
 * - LF: line 증가, column 은 1 로 리셋
 * - '--': 그 줄의 끝(\n 직전 또는 EOF)까지 건너뜀 (개행 문자 자체는 while 밖에서 처리)
 */
static void skip_ws_and_comments(Lexer *lex) {//공백과 주석을 제거한다.
    while (lex->pos < lex->len) {//토큰화할 문자열의 끝에 도달할 때까지 반복한다.
        unsigned char c = (unsigned char)lex->src[lex->pos];//현재 문자를 가져온다.
        if (c == ' ' || c == '\t' || c == '\r') {//만약 공백이라면, 열을 증가시킨다.
            lex->pos++;//현재 문자의 위치를 증가시킨다.
            lex->column++;//열을 증가시킨다.
        } else if (c == '\n') {//만약 개행 문자라면, 줄을 증가시키고 열을 1로 리셋한다.
            lex->pos++;//현재 문자의 위치를 증가시킨다.
            lex->line++;//줄을 증가시킨다.
            lex->column = 1;//열을 1로 리셋한다.
        } else if (c == '-' && lex->pos + 1 < lex->len  && lex->src[lex->pos + 1] == '-') {//만약 주석 문자라면, 주석 문자의 위치 만큼만 건너뛰고 열을 2로 증가시킨다.
            lex->pos += 2;//주석 문자의 위치를 증가시킨다.
            lex->column += 2;//열을 2로 증가시킨다.
            while (lex->pos < lex->len && lex->src[lex->pos] != '\n') {
                lex->pos++;//현재 문자의 위치를 증가시킨다.
                lex->column++;//열을 증가시킨다.
            }
        } else {
            break;//반복문을 종료한다.
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 공개: 초기화                                                                */
/* -------------------------------------------------------------------------- */

void lexer_init(Lexer *lex, const char *src, size_t len) {//토큰화할 문자열을 초기화한다.
    lex->src = src;//토큰화할 문자열을 설정한다.
    lex->len = len;//토큰화할 문자열의 길이를 설정한다.
    lex->pos = 0;//현재 문자의 위치를 설정한다.
    lex->line = 1;//줄을 설정한다.
    lex->column = 1;//열을 설정한다.
}

/* -------------------------------------------------------------------------- */
/* 작은따옴표 문자열 — '' 를 한 글자 ' 로 이스케이프 해제해 str_buf 에 적재   */
/* -------------------------------------------------------------------------- */

/**
 * 현재 위치가 작은따옴표로 시작한다고 가정하고 문자열 리터럴을 읽는다.
 * 성공 시 tok 에 TOKEN_STRING, text=lex->str_buf, text_len=디코딩 바이트 수.
 * 실패(닫히지 않음, 버퍼 초과) 시 TOKEN_ERROR, 반환 -1.
 * 주: str_buf 는 널 종료를 보장하지 않음; tok->text_len 으로 길이 판단.
 */
static int decode_string(Lexer *lex, Token *tok) {//작은따옴표 문자열을 디코딩한다.
    size_t out = 0;//디코딩 바이트 수를 설정한다.
    if (lex->pos >= lex->len || lex->src[lex->pos] != '\'') {//만약 작은따옴표로 시작하지 않는다면, 에러를 반환한다.
        tok->kind = TOKEN_ERROR;//에러 토큰을 설정한다.
        return -1;//에러를 반환한다.
    }
    /* 여는 따옴표 소비 */
    lex->pos++;//현재 문자의 위치를 증가시킨다.
    lex->column++;//열을 증가시킨다.
    while (lex->pos < lex->len) {//토큰화할 문자열의 끝에 도달할 때까지 반복한다.
        char c = lex->src[lex->pos];//현재 문자를 가져온다.
        if (c == '\'') {
            /* '' → 내부에 리터럴 ' 하나 */
            if (lex->pos + 1 < lex->len && lex->src[lex->pos + 1] == '\'') {//만약 다음 문자가 작은따옴표라면, 작은따옴표의 위치를 증가시킨다.
                if (out + 1 >= LEXER_STR_CAP) {//만약 디코딩 바이트 수가 최대 바이트 수를 초과한다면, 에러를 반환한다.
                    tok->kind = TOKEN_ERROR;//에러 토큰을 설정한다.
                    return -1;//에러를 반환한다.
                }
                lex->str_buf[out++] = '\'';//작은따옴표를 디코딩 바이트 수에 추가한다.
                lex->pos += 2;//작은따옴표의 위치를 증가시킨다.
                lex->column += 2;//열을 2로 증가시킨다.
            } else {//만약 다음 문자가 작은따옴표가 아니라면, 작은따옴표의 위치를 증가시킨다.
                lex->pos++;//현재 문자의 위치를 증가시킨다.
                lex->column++;//열을 증가시킨다.
                tok->kind = TOKEN_STRING;//문자열 토큰을 설정한다.
                tok->text = lex->str_buf;//문자열 토큰의 텍스트를 설정한다.
                tok->text_len = out;//문자열 토큰의 텍스트 길이를 설정한다.
                return 0;//문자열 토큰을 반환한다.
            }
        } else {
            /* 일반 문자(개행 포함: SQL에서 허용되면 문자열 내부에 LF 저장) */
            if (c == '\n') {//만약 개행 문자라면, 줄을 증가시키고 열을 1로 리셋한다.    
                lex->line++;//줄을 증가시킨다.
                lex->column = 1;//열을 1로 리셋한다.
            } else {
                lex->column++;//열을 증가시킨다.
            }
            if (out + 1 >= LEXER_STR_CAP) {//만약 디코딩 바이트 수가 최대 바이트 수를 초과한다면, 에러를 반환한다.
                tok->kind = TOKEN_ERROR;
                return -1;//에러를 반환한다.
            }
            lex->str_buf[out++] = c;//문자를 디코딩 바이트 수에 추가한다.
            lex->pos++;//현재 문자의 위치를 증가시킨다.
        }
    }
    /* EOF 까지 왔는데 닫는 ' 없음 */
    tok->kind = TOKEN_ERROR;
    return -1;
}

/* -------------------------------------------------------------------------- */
/* 공개: 다음 토큰 하나                                                        */
/* -------------------------------------------------------------------------- */

int lexer_next(Lexer *lex, Token *tok) {
    /* 의미 있는 토큰 직전까지 공백·주석 제거 */
    skip_ws_and_comments(lex);// 공백과 주석을 제거한다.

    if (lex->pos >= lex->len) {//만약 토큰화할 문자열의 끝에 도달했다면, EOF 토큰을 반환한다.
        tok->kind = TOKEN_EOF;//EOF 토큰의 종류를 설정한다.
        tok->text = NULL;//EOF 토큰의 텍스트를 설정한다.
        tok->text_len = 0;//EOF 토큰의 텍스트 길이를 설정한다.
        tok->line = lex->line;//EOF 토큰이 시작된 줄을 설정한다.
        tok->column = lex->column;//EOF 토큰이 시작된 열을 설정한다.
        return 0;//EOF 토큰을 반환한다.
    }

    /* 에러 메시지용: 이 토큰이 시작된 줄·열(토큰 consume 전에 스냅샷) */
    unsigned start_line = lex->line; //토큰이 시작된 줄을 설정한다.
    unsigned start_col = lex->column; //토큰이 시작된 열을 설정한다.
    char c = lex->src[lex->pos]; //현재 문자를 가져온다.

    /* 식별자 또는 키워드: 연속 is_ident_char 최장 매칭 후 keyword_kind */
    if (is_ident_start(c)) { // 만약 식별자 시작 문자라면
        size_t start = lex->pos;//식별자 시작 문자의 위치를 설정한다.
        lex->pos++;//현재 문자의 위치를 증가시킨다.
        lex->column++;//열을 증가시킨다.
        while (lex->pos < lex->len && is_ident_char(lex->src[lex->pos])) {//현재 문자가 식별자 문자라면, 식별자 문자의 위치를 증가시킨다.
            lex->pos++;//현재 문자의 위치를 증가시킨다.
            lex->column++;//열을 증가시킨다.
        }
        size_t n = lex->pos - start;//식별자 문자의 길이를 설정한다.
        TokenKind kw;//키워드의 종류를 설정한다.
        if (keyword_kind(lex->src + start, n, &kw)) {//만약 키워드라면, 키워드의 종류를 설정한다.
            tok->kind = kw;//키워드의 종류를 설정한다.
        } else {//만약 키워드가 아니라면, 식별자 토큰으로 설정한다.
            tok->kind = TOKEN_IDENTIFIER;//식별자 토큰으로 설정한다.
        }
        tok->text = lex->src + start;// 토큰의 텍스트를 설정한다.
        tok->text_len = n;//토큰의 텍스트 길이를 설정한다.
        tok->line = start_line;//토큰이 시작된 줄을 설정한다.
        tok->column = start_col;//토큰이 시작된 열을 설정한다.
        return 0;//토큰을 반환한다.
    }

    /* 부호 있는 정수: +/- 다음 글자가 숫자일 때만 부호를 정수 토큰에 포함 */
    if (c == '+' || c == '-') {
        if (lex->pos + 1 < lex->len && isdigit((unsigned char)lex->src[lex->pos + 1])) {//만약 다음 문자가 숫자라면, 숫자의 위치를 증가시킨다.
            size_t start = lex->pos;//숫자의 시작 위치를 설정한다.
            lex->pos++;//현재 문자의 위치를 증가시킨다.
            lex->column++;//열을 증가시킨다.
            while (lex->pos < lex->len && isdigit((unsigned char)lex->src[lex->pos])) {//현재 문자가 숫자라면, 숫자의 위치를 증가시킨다.
                lex->pos++;//현재 문자의 위치를 증가시킨다.
                lex->column++;//열을 증가시킨다.
            }
            tok->kind = TOKEN_INTEGER;//토큰의 종류를 설정한다.
            tok->text = lex->src + start;//토큰의 텍스트를 설정한다.
            tok->text_len = lex->pos - start;//토큰의 텍스트 길이를 설정한다.
            tok->line = start_line;//토큰이 시작된 줄을 설정한다.
            tok->column = start_col;//토큰이 시작된 열을 설정한다.
            return 0;//토큰을 반환한다.
        }
        /* 단독 + / - (향후 연산자 확장용; MVP 에서는 드묾) */
        tok->kind = (c == '+') ? TOKEN_PLUS : TOKEN_MINUS;//토큰의 종류를 설정한다.
        tok->text = lex->src + lex->pos;//토큰의 텍스트를 설정한다.
        tok->text_len = 1;//토큰의 텍스트 길이를 설정한다.
        lex->pos++;//현재 문자의 위치를 증가시킨다.
        lex->column++;//열을 증가시킨다.
        tok->line = start_line;//토큰이 시작된 줄을 설정한다.
        tok->column = start_col;//토큰이 시작된 열을 설정한다.
        return 0;//토큰을 반환한다.
    }

    /* 부호 없는 정수 */
    if (isdigit((unsigned char)c)) { //만약 현재 문자가 숫자라면
        size_t start = lex->pos;//숫자의 시작 위치를 설정한다.
        while (lex->pos < lex->len && isdigit((unsigned char)lex->src[lex->pos])) {//현재 문자가 숫자라면, 숫자의 위치를 증가시킨다.
            lex->pos++;//현재 문자의 위치를 증가시킨다.
            lex->column++;//열을 증가시킨다.
        }
        tok->kind = TOKEN_INTEGER;//토큰의 종류를 설정한다.
        tok->text = lex->src + start;//토큰의 텍스트를 설정한다.
        tok->text_len = lex->pos - start;//토큰의 텍스트 길이를 설정한다.
        tok->line = start_line;//토큰이 시작된 줄을 설정한다.
        tok->column = start_col;//토큰이 시작된 열을 설정한다.
        return 0;//토큰을 반환한다.
    }

    /* 작은따옴표 문자열 */
    if (c == '\'') {
        tok->line = start_line;
        tok->column = start_col;
        return decode_string(lex, tok);
    }

    /* 단일 문자 구두점·* (지원 목록에 없으면 TOKEN_ERROR) */
    tok->line = start_line;
    tok->column = start_col;
    tok->text = lex->src + lex->pos;
    tok->text_len = 1;
    switch (c) {
    case '(':
        tok->kind = TOKEN_LPAREN;
        break;
    case ')':
        tok->kind = TOKEN_RPAREN;
        break;
    case ',':
        tok->kind = TOKEN_COMMA;
        break;
    case ';':
        tok->kind = TOKEN_SEMICOLON;
        break;
    case '*':
        tok->kind = TOKEN_STAR;
        break;
    case '=':
        tok->kind = TOKEN_EQ;
        break;
    default:
        tok->kind = TOKEN_ERROR;
        return -1;
    }
    lex->pos++;
    lex->column++;
    return 0;
}
