#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdint.h>

/** VALUES 절 리터럴 종류 (docs/03-api-reference.md §4.1). */
typedef enum {
    SQL_VALUE_INT,    //정수
    SQL_VALUE_STRING, //문자열
    SQL_VALUE_NULL,   //NULL
} SqlValueKind; //VALUES 절 리터럴 종류를 정의한다.

/**
 * 한 칸의 값. INT/STRING 은 text 가 널 종료 복사본(executor 가 파싱).
 * NULL 은 kind 만 SQL_VALUE_NULL, text 는 NULL.
 */
typedef struct {
    SqlValueKind kind; //리터럴 종류
    char *text;        //리터럴 텍스트
} SqlValue; //한 칸의 값을 정의한다.

/**
 * INSERT INTO <table> VALUES (...); 에 대응하는 AST (MVP).
 * 소유권: 필드는 heap; ast_insert_stmt_free 로 해제.
 */
typedef struct {
    char *table;        //테이블 이름
    SqlValue *values;   //값의 배열
    size_t value_count; //값의 개수
} InsertStmt; //INSERT AST를 정의한다.

/** SELECT 문 AST (단계 4). */
typedef struct {
    int select_all;       //1이면 SELECT *
    char **columns;       //select_all==0 일 때 컬럼 목록
    size_t column_count;  //컬럼 개수
    char *table;          //FROM 테이블 이름
    /* WEEK7: WHERE id = <정수> 만 지원; 없으면 has_where_id_eq==0 */
    int has_where_id_eq;
    int64_t where_id_value;
} SelectStmt;

void ast_insert_stmt_free(InsertStmt *stmt); //INSERT AST 해제
void ast_select_stmt_free(SelectStmt *stmt); //SELECT AST 해제

#endif
