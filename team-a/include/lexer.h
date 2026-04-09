#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    /* --- 스트림 끝 / 오류 --- */
    TOKEN_EOF = 0,     /* 입력 끝 — 더 읽을 문자 없음 */
    TOKEN_ERROR,       /* 토큰화 실패(미지원 문자, 문자열 미종료 등) */

    /* --- SQL 키워드 (식별자 모양이어도 예약어와 대소문자 무시로 매칭되면 여기로 분류) --- */
    TOKEN_INSERT, /* 행 추가 문장 시작: `INSERT INTO <테이블> VALUES (...);` 패턴의 첫 단어 (docs §4.1) */
    TOKEN_INTO,   /* INSERT 다음: 뒤이어 오는 식별자가 대상 테이블 이름임을 표시 */
    TOKEN_VALUES, /* INSERT 문 안에서 괄호 값 목록 앞: `VALUES (v1, v2, ...)` 의 VALUES */
    TOKEN_SELECT, /* 조회 문장 시작: `SELECT *` 또는 `SELECT col,...` 후 FROM 으로 이어짐 (§4.2) */
    TOKEN_FROM,   /* SELECT 에서 테이블 지정: `FROM <테이블>` 의 FROM */
    TOKEN_NULL,   /* 값 자리의 NULL 리터럴; 식별자 `null` 과 동일 규칙으로 읽혀 구분(대소문자 무시) */

    /* --- 식별자·리터럴 --- */
    TOKEN_IDENTIFIER,  /* 테이블·컬럼 등 (문서의 식별자 규칙) */
    TOKEN_INTEGER,     /* 정수 [+-]?[0-9]+ */
    TOKEN_STRING,      /* '...' 문자열(내용은 이스케이프 해제) */

    /* --- 구두점·단일 문자 연산자 --- */
    TOKEN_LPAREN,      /* ( */
    TOKEN_RPAREN,      /* ) */
    TOKEN_COMMA,       /* , */
    TOKEN_SEMICOLON,   /* ; */
    TOKEN_STAR,        /* * */
    TOKEN_PLUS,        /* 단독 + */
    TOKEN_MINUS,       /* 단독 - */
} TokenKind;

typedef struct {
    TokenKind kind;     /* 이 토큰의 종류(TokenKind 상수) */
    const char *text;   /* 토큰 텍스트 시작: IDENT/INTEGER는 원본 src 구간, STRING은 Lexer.str_buf(다음 lexer_next 전까지) */
    size_t text_len;    /* text 바이트 길이(STRING은 이스케이프 해제 후 길이) */
    unsigned line;      /* 토큰이 시작된 소스 줄(1부터; 오류 메시지용) */
    unsigned column;    /* 토큰이 시작된 열(1부터; 오류 메시지용) */
} Token;

/* TOKEN_STRING 이스케이프 해제 결과를 str_buf에 담을 때의 최대 바이트 수(널 종료 보장 없음; text_len 사용) */
#define LEXER_STR_CAP 4096

typedef struct Lexer {
    const char *src;    /* 토큰화할 SQL 원문(바이트 나열; 소유권은 호출자, Lexer는 읽기만) */
    size_t len;         /* src 길이(널 종료 문자열이 아니어도 됨; [0, len) 만 사용) */
    size_t pos;         /* 다음에 읽을 바이트 오프셋(0 ~ len) */
    unsigned line;      /* 현재 pos 기준 줄 번호(1부터; 줄바꿈 소비 시 증가) */
    unsigned column;    /* 현재 pos 기준 열(1부터; 글자 소비 시 증가, 개행 시 1로 리셋) */
    char str_buf[LEXER_STR_CAP]; /* 직전 STRING 토큰 등 이스케이프 해제 문자열을 담는 내부 버퍼 */
} Lexer;

/* Lexer 를 src[0..len) 로 초기화; pos=0, line=1, column=1 로 맞춘다. */
void lexer_init(Lexer *lex, const char *src, size_t len);

/**
 * 다음 토큰을 tok에 채운다.
 * @return 0 성공, -1 이전 토큰화 불가(예: 문자열 미종료). tok->kind == TOKEN_ERROR 일 수 있음.
 */
int lexer_next(Lexer *lex, Token *tok); //단어 단위의 "한 걸음" 더 나아가는 함수

#endif
