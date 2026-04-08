#ifndef AST_H
#define AST_H

#include <stddef.h>

/** VALUES 절 리터럴 종류 (docs/03-api-reference.md §4.1). */
typedef enum {
    SQL_VALUE_INT,
    SQL_VALUE_STRING,
    SQL_VALUE_NULL,
} SqlValueKind;

/**
 * 한 칸의 값. INT/STRING 은 text 가 널 종료 복사본(executor 가 파싱).
 * NULL 은 kind 만 SQL_VALUE_NULL, text 는 NULL.
 */
typedef struct {
    SqlValueKind kind;
    char *text;
} SqlValue;

/**
 * INSERT INTO <table> VALUES (...); 에 대응하는 AST (MVP).
 * 소유권: 필드는 heap; ast_insert_stmt_free 로 해제.
 */
typedef struct {
    char *table;
    SqlValue *values;
    size_t value_count;
} InsertStmt;

void ast_insert_stmt_free(InsertStmt *stmt);

#endif
