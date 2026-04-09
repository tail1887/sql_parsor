#include "executor.h"
#include "parser.h"
#include "sql_file_reader.h"
#include "statement_splitter.h"
#include "tokenizer.h"

#include <stdio.h>
#include <stdlib.h>

#define STUDENT_CSV_PATH "data/student.csv"
#define ENTRY_LOG_BIN_PATH "data/entry_log.bin"

static int execute_sql_statements(const StatementList *statements)
{
    size_t i;

    /*
     * splitter가 나눈 문장 문자열을 하나씩 꺼내서
     * tokenizer -> parser -> executor 순서로 실행한다.
     *
     * 문장 하나라도 실패하면 즉시 중단한다.
     * 이것이 명세의 "에러 시 이후 문장 실행 중단" 규칙이다.
     */
    for (i = 0U; i < statements->count; i++) {
        TokenList tokens;
        Statement statement;
        int parsed_successfully;

        tokens.items = NULL;
        tokens.count = 0U;
        parsed_successfully = 0;

        if (tokenize_sql(statements->items[i], &tokens) != 0) {
            fprintf(stderr, "failed to tokenize statement\n");
            free_token_list(&tokens);
            return 1;
        }

        if (parse_statement(&tokens, &statement) != 0) {
            fprintf(stderr, "failed to parse statement\n");
            free_token_list(&tokens);
            return 1;
        }

        parsed_successfully = 1;

        if (execute_statement(
                &statement,
                STUDENT_CSV_PATH,
                ENTRY_LOG_BIN_PATH,
                stdout,
                stderr) != 0) {
            if (parsed_successfully) {
                free_statement(&statement);
            }

            free_token_list(&tokens);
            return 1;
        }

        if (parsed_successfully) {
            free_statement(&statement);
        }

        free_token_list(&tokens);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *sql_file_path;
    char *sql_text;
    StatementList statements;

    /*
     * CLI 형식은 Step 1과 동일하다.
     * 인자는 SQL 파일 경로 하나만 받는다.
     */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <sql_file_path>\n", argv[0]);
        return 1;
    }

    sql_file_path = argv[1];

    /* 아직 메모리를 받은 적 없는 안전한 초기 상태로 둔다. */
    sql_text = NULL;
    statements.items = NULL;
    statements.count = 0U;

    /* SQL 파일 전체를 하나의 문자열로 읽는다. */
    if (read_text_file(sql_file_path, &sql_text) != 0) {
        fprintf(stderr, "failed to read sql file: %s\n", sql_file_path);
        return 1;
    }

    /* 전체 문자열을 세미콜론 기준으로 문장 여러 개로 나눈다. */
    if (split_sql_statements(sql_text, &statements) != 0) {
        fprintf(stderr, "failed to split statements: every statement must end with ';'\n");
        free(sql_text);
        return 1;
    }

    /*
     * Step 4부터는 문장 문자열을 그대로 출력하지 않는다.
     * 각 문장을 실제로 tokenizing / parsing / execution까지 진행한다.
     */
    if (execute_sql_statements(&statements) != 0) {
        free_statement_list(&statements);
        free(sql_text);
        return 1;
    }

    /* malloc으로 받은 메모리는 마지막에 직접 해제해야 한다. */
    free_statement_list(&statements);
    free(sql_text);
    return 0;
}
