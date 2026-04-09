#include "parser.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * Parser는 tokenizer가 만든 토큰 배열을 읽어
 * "지원하는 문장 패턴 중 어떤 것인지"를 판별하고 AST를 만든다.
 *
 * Step 3에서는 오직 아래 5개 패턴만 허용한다.
 * 1. INSERT INTO STUDENT_CSV VALUES (id, 'name', class);
 * 2. INSERT INTO ENTRY_LOG_BIN VALUES ('datetime', id);
 * 3. SELECT * FROM STUDENT_CSV;
 * 4. SELECT * FROM STUDENT_CSV WHERE id = <int>;
 * 5. SELECT * FROM ENTRY_LOG_BIN WHERE id = <int>;
 */

typedef struct {
    const TokenList *tokens;
    size_t current_index;
} Parser;

static void initialize_statement(Statement *statement)
{
    statement->type = STATEMENT_SELECT_STUDENT_ALL;
    statement->data.insert_student.name = NULL;
}

static const Token *current_token(const Parser *parser)
{
    if (parser->current_index >= parser->tokens->count) {
        return NULL;
    }

    return &parser->tokens->items[parser->current_index];
}

static int current_token_is(const Parser *parser, TokenType expected_type)
{
    const Token *token;

    token = current_token(parser);
    return token != NULL && token->type == expected_type;
}

static int current_identifier_is(const Parser *parser, const char *expected_text)
{
    const Token *token;

    token = current_token(parser);
    if (token == NULL || token->type != TOKEN_IDENTIFIER) {
        return 0;
    }

    return strcmp(token->text, expected_text) == 0;
}

static int advance_parser(Parser *parser)
{
    if (parser->current_index >= parser->tokens->count) {
        return 1;
    }

    parser->current_index += 1U;
    return 0;
}

static int consume_token(Parser *parser, TokenType expected_type)
{
    /*
     * consume_token은 "현재 토큰이 내가 기대한 종류인지" 검사하고,
     * 맞으면 다음 토큰으로 한 칸 이동한다.
     * parser에서 가장 자주 쓰는 기본 동작이다.
     */
    if (!current_token_is(parser, expected_type)) {
        return 1;
    }

    return advance_parser(parser);
}

static int duplicate_token_text(const Token *token, char **out_text)
{
    size_t length;
    char *copy;

    if (token == NULL || out_text == NULL) {
        return 1;
    }

    length = strlen(token->text);
    copy = (char *)malloc(length + 1U);
    if (copy == NULL) {
        return 1;
    }

    memcpy(copy, token->text, length + 1U);
    *out_text = copy;
    return 0;
}

static int parse_int_token_value(const Token *token, int *out_value)
{
    long parsed_value;
    char *end_ptr;

    if (token == NULL || out_value == NULL || token->type != TOKEN_INTEGER) {
        return 1;
    }

    parsed_value = strtol(token->text, &end_ptr, 10);
    if (*end_ptr != '\0') {
        return 1;
    }

    if (parsed_value < INT_MIN || parsed_value > INT_MAX) {
        return 1;
    }

    *out_value = (int)parsed_value;
    return 0;
}

static int parse_required_integer(Parser *parser, int *out_value)
{
    const Token *token;

    token = current_token(parser);
    if (token == NULL || token->type != TOKEN_INTEGER) {
        return 1;
    }

    if (parse_int_token_value(token, out_value) != 0) {
        return 1;
    }

    return advance_parser(parser);
}

static int parse_required_string(Parser *parser, char **out_text)
{
    const Token *token;

    token = current_token(parser);
    if (token == NULL || token->type != TOKEN_STRING) {
        return 1;
    }

    if (duplicate_token_text(token, out_text) != 0) {
        return 1;
    }

    return advance_parser(parser);
}

static int parse_where_id_equals_integer(Parser *parser, int *out_id)
{
    /*
     * Step 3 명세에서 WHERE는 오직 "WHERE id = <int>" 하나만 허용한다.
     * 따라서 id 외의 다른 컬럼명이나 비교식은 모두 실패 처리한다.
     */
    if (consume_token(parser, TOKEN_WHERE) != 0) {
        return 1;
    }

    if (!current_identifier_is(parser, "id")) {
        return 1;
    }

    if (advance_parser(parser) != 0) {
        return 1;
    }

    if (consume_token(parser, TOKEN_EQUAL) != 0) {
        return 1;
    }

    return parse_required_integer(parser, out_id);
}

static int parse_insert_student(Parser *parser, Statement *statement)
{
    char *name;

    name = NULL;

    /*
     * 지금 시점에서는 이미 INSERT INTO STUDENT_CSV VALUES 까지 확인한 상태다.
     * 남은 부분은 ( INTEGER , STRING , INTEGER ) ; 형태인지 검사하면 된다.
     */
    if (consume_token(parser, TOKEN_LPAREN) != 0) {
        return 1;
    }

    if (parse_required_integer(parser, &statement->data.insert_student.id) != 0) {
        return 1;
    }

    if (consume_token(parser, TOKEN_COMMA) != 0) {
        return 1;
    }

    if (parse_required_string(parser, &name) != 0) {
        return 1;
    }

    if (consume_token(parser, TOKEN_COMMA) != 0) {
        free(name);
        return 1;
    }

    if (parse_required_integer(parser, &statement->data.insert_student.class_number) != 0) {
        free(name);
        return 1;
    }

    if (consume_token(parser, TOKEN_RPAREN) != 0) {
        free(name);
        return 1;
    }

    if (consume_token(parser, TOKEN_SEMICOLON) != 0) {
        free(name);
        return 1;
    }

    statement->type = STATEMENT_INSERT_STUDENT;
    statement->data.insert_student.name = name;
    return 0;
}

static int parse_insert_entry_log(Parser *parser, Statement *statement)
{
    char *entered_at;

    entered_at = NULL;

    if (consume_token(parser, TOKEN_LPAREN) != 0) {
        return 1;
    }

    if (parse_required_string(parser, &entered_at) != 0) {
        return 1;
    }

    if (consume_token(parser, TOKEN_COMMA) != 0) {
        free(entered_at);
        return 1;
    }

    if (parse_required_integer(parser, &statement->data.insert_entry_log.id) != 0) {
        free(entered_at);
        return 1;
    }

    if (consume_token(parser, TOKEN_RPAREN) != 0) {
        free(entered_at);
        return 1;
    }

    if (consume_token(parser, TOKEN_SEMICOLON) != 0) {
        free(entered_at);
        return 1;
    }

    statement->type = STATEMENT_INSERT_ENTRY_LOG;
    statement->data.insert_entry_log.entered_at = entered_at;
    return 0;
}

static int parse_insert_statement(Parser *parser, Statement *statement)
{
    /*
     * INSERT 문장은 지금 프로젝트에서 두 테이블만 허용한다.
     * 그래서 table 이름을 먼저 보고,
     * STUDENT_CSV면 학생 INSERT 패턴,
     * ENTRY_LOG_BIN이면 입장 기록 INSERT 패턴으로 분기한다.
     */
    if (consume_token(parser, TOKEN_INSERT) != 0) {
        return 1;
    }

    if (consume_token(parser, TOKEN_INTO) != 0) {
        return 1;
    }

    if (current_identifier_is(parser, "STUDENT_CSV")) {
        if (advance_parser(parser) != 0) {
            return 1;
        }

        if (consume_token(parser, TOKEN_VALUES) != 0) {
            return 1;
        }

        return parse_insert_student(parser, statement);
    }

    if (current_identifier_is(parser, "ENTRY_LOG_BIN")) {
        if (advance_parser(parser) != 0) {
            return 1;
        }

        if (consume_token(parser, TOKEN_VALUES) != 0) {
            return 1;
        }

        return parse_insert_entry_log(parser, statement);
    }

    return 1;
}

static int parse_select_statement(Parser *parser, Statement *statement)
{
    /*
     * SELECT 문장은 Step 3에서 항상 "SELECT * FROM ..."만 허용한다.
     * 따라서 SELECT 다음에는 반드시 STAR가 와야 하고,
     * 그 뒤에 어떤 테이블인지에 따라 허용 패턴을 좁혀서 본다.
     */
    if (consume_token(parser, TOKEN_SELECT) != 0) {
        return 1;
    }

    if (consume_token(parser, TOKEN_STAR) != 0) {
        return 1;
    }

    if (consume_token(parser, TOKEN_FROM) != 0) {
        return 1;
    }

    if (current_identifier_is(parser, "STUDENT_CSV")) {
        if (advance_parser(parser) != 0) {
            return 1;
        }

        if (current_token_is(parser, TOKEN_SEMICOLON)) {
            if (consume_token(parser, TOKEN_SEMICOLON) != 0) {
                return 1;
            }

            statement->type = STATEMENT_SELECT_STUDENT_ALL;
            return 0;
        }

        if (parse_where_id_equals_integer(parser, &statement->data.select_student_by_id.id) != 0) {
            return 1;
        }

        if (consume_token(parser, TOKEN_SEMICOLON) != 0) {
            return 1;
        }

        statement->type = STATEMENT_SELECT_STUDENT_BY_ID;
        return 0;
    }

    if (current_identifier_is(parser, "ENTRY_LOG_BIN")) {
        if (advance_parser(parser) != 0) {
            return 1;
        }

        if (parse_where_id_equals_integer(parser, &statement->data.select_entry_log_by_id.id) != 0) {
            return 1;
        }

        if (consume_token(parser, TOKEN_SEMICOLON) != 0) {
            return 1;
        }

        statement->type = STATEMENT_SELECT_ENTRY_LOG_BY_ID;
        return 0;
    }

    return 1;
}

void free_statement(Statement *statement)
{
    if (statement == NULL) {
        return;
    }

    /*
     * AST 안에서 동적 메모리를 쓰는 곳은 문자열 필드뿐이다.
     * 따라서 INSERT_STUDENT.name, INSERT_ENTRY_LOG.entered_at만 해제하면 된다.
     */
    if (statement->type == STATEMENT_INSERT_STUDENT) {
        free(statement->data.insert_student.name);
        statement->data.insert_student.name = NULL;
    } else if (statement->type == STATEMENT_INSERT_ENTRY_LOG) {
        free(statement->data.insert_entry_log.entered_at);
        statement->data.insert_entry_log.entered_at = NULL;
    }
}

const char *statement_type_name(StatementType type)
{
    switch (type) {
    case STATEMENT_INSERT_STUDENT:
        return "STATEMENT_INSERT_STUDENT";
    case STATEMENT_INSERT_ENTRY_LOG:
        return "STATEMENT_INSERT_ENTRY_LOG";
    case STATEMENT_SELECT_STUDENT_ALL:
        return "STATEMENT_SELECT_STUDENT_ALL";
    case STATEMENT_SELECT_STUDENT_BY_ID:
        return "STATEMENT_SELECT_STUDENT_BY_ID";
    case STATEMENT_SELECT_ENTRY_LOG_BY_ID:
        return "STATEMENT_SELECT_ENTRY_LOG_BY_ID";
    default:
        return "STATEMENT_UNKNOWN";
    }
}

int parse_statement(const TokenList *tokens, Statement *out_statement)
{
    Parser parser;
    Statement statement;

    if (tokens == NULL || out_statement == NULL || tokens->count == 0U) {
        return 1;
    }

    /*
     * 실패했을 때도 free_statement를 안전하게 부를 수 있도록
     * 문자열 포인터들을 먼저 NULL 상태로 초기화해 둔다.
     */
    initialize_statement(&statement);

    parser.tokens = tokens;
    parser.current_index = 0U;

    /*
     * 첫 토큰을 보고 INSERT 문장인지 SELECT 문장인지 분기한다.
     * 그 외의 시작 토큰은 Step 3 명세 밖이므로 실패 처리한다.
     */
    if (current_token_is(&parser, TOKEN_INSERT)) {
        if (parse_insert_statement(&parser, &statement) != 0) {
            free_statement(&statement);
            return 1;
        }
    } else if (current_token_is(&parser, TOKEN_SELECT)) {
        if (parse_select_statement(&parser, &statement) != 0) {
            free_statement(&statement);
            return 1;
        }
    } else {
        return 1;
    }

    /*
     * 문장 하나를 다 읽고 나면 더 남은 토큰이 없어야 한다.
     * 남아 있다면 Step 3에서 허용하지 않는 여분 문법이 붙은 경우로 본다.
     */
    if (parser.current_index != tokens->count) {
        free_statement(&statement);
        return 1;
    }

    *out_statement = statement;
    return 0;
}
