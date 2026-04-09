#ifndef PARSER_H
#define PARSER_H

#include "tokenizer.h"

/*
 * Step 3의 parser는 지원하는 SQL 5가지 패턴만 AST로 변환한다.
 * 범용 SQL 문법을 다루지 않고, 명세에 있는 최소 구조만 유지한다.
 */
typedef enum {
    STATEMENT_INSERT_STUDENT,
    STATEMENT_INSERT_ENTRY_LOG,
    STATEMENT_SELECT_STUDENT_ALL,
    STATEMENT_SELECT_STUDENT_BY_ID,
    STATEMENT_SELECT_ENTRY_LOG_BY_ID
} StatementType;

/*
 * INSERT INTO STUDENT_CSV VALUES (id, 'name', class);
 * 에 필요한 데이터만 모아 둔 구조체다.
 */
typedef struct {
    int id;
    char *name;
    int class_number;
} InsertStudentData;

/*
 * INSERT INTO ENTRY_LOG_BIN VALUES ('YYYY-MM-DD HH:MM:SS', id);
 * 에 필요한 데이터만 모아 둔 구조체다.
 */
typedef struct {
    char *entered_at;
    int id;
} InsertEntryLogData;

/*
 * SELECT ... WHERE id = <int>;
 * 형태는 결국 정수 id 하나만 필요하므로 가장 작은 구조체로 둔다.
 */
typedef struct {
    int id;
} SelectByIdData;

/*
 * Statement는 "이 문장이 어떤 종류인가"와
 * "그 문장에 필요한 실제 값들"을 함께 저장하는 최소 AST다.
 *
 * type을 먼저 보고,
 * 그 type에 맞는 union 필드만 사용하면 된다.
 */
typedef struct {
    StatementType type;
    union {
        InsertStudentData insert_student;
        InsertEntryLogData insert_entry_log;
        SelectByIdData select_student_by_id;
        SelectByIdData select_entry_log_by_id;
    } data;
} Statement;

int parse_statement(const TokenList *tokens, Statement *out_statement);
void free_statement(Statement *statement);
const char *statement_type_name(StatementType type);

#endif
