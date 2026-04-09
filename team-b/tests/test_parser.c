#include "parser.h"
#include "tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;

static void fail_test(const char *test_name, const char *message)
{
    fprintf(stderr, "%s: %s\n", test_name, message);
    exit(1);
}

static void assert_string_equals(
    const char *test_name,
    const char *expected,
    const char *actual)
{
    if (strcmp(expected, actual) != 0) {
        fprintf(
            stderr,
            "%s: expected [%s] but got [%s]\n",
            test_name,
            expected,
            actual);
        exit(1);
    }
}

static void assert_statement_type_equals(
    const char *test_name,
    StatementType expected_type,
    StatementType actual_type)
{
    if (expected_type != actual_type) {
        fprintf(
            stderr,
            "%s: expected statement type %s but got %s\n",
            test_name,
            statement_type_name(expected_type),
            statement_type_name(actual_type));
        exit(1);
    }
}

static int tokenize_and_parse(
    const char *sql_text,
    TokenList *out_tokens,
    Statement *out_statement)
{
    if (tokenize_sql(sql_text, out_tokens) != 0) {
        return 1;
    }

    if (parse_statement(out_tokens, out_statement) != 0) {
        free_token_list(out_tokens);
        return 1;
    }

    return 0;
}

#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            fail_test(__func__, message); \
        } \
    } while (0)

#define ASSERT_STRING_EQ(expected, actual) \
    do { \
        assert_string_equals(__func__, expected, actual); \
    } while (0)

#define ASSERT_STATEMENT_TYPE(expected, actual) \
    do { \
        assert_statement_type_equals(__func__, expected, actual); \
    } while (0)

#define RUN_TEST(test_function) \
    do { \
        test_function(); \
        tests_run += 1; \
    } while (0)

static void test_parse_insert_student_statement(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_and_parse(
            "INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);",
            &tokens,
            &statement) == 0,
        "student INSERT should parse successfully");
    ASSERT_STATEMENT_TYPE(STATEMENT_INSERT_STUDENT, statement.type);
    ASSERT_TRUE(statement.data.insert_student.id == 302, "student id should be 302");
    ASSERT_STRING_EQ("Kim", statement.data.insert_student.name);
    ASSERT_TRUE(
        statement.data.insert_student.class_number == 302,
        "student class should be 302");

    free_statement(&statement);
    free_token_list(&tokens);
}

static void test_parse_insert_entry_log_statement(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_and_parse(
            "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 302);",
            &tokens,
            &statement) == 0,
        "entry log INSERT should parse successfully");
    ASSERT_STATEMENT_TYPE(STATEMENT_INSERT_ENTRY_LOG, statement.type);
    ASSERT_STRING_EQ("2026-04-08 09:00:00", statement.data.insert_entry_log.entered_at);
    ASSERT_TRUE(statement.data.insert_entry_log.id == 302, "entry log id should be 302");

    free_statement(&statement);
    free_token_list(&tokens);
}

static void test_parse_select_student_all_statement(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_and_parse("SELECT * FROM STUDENT_CSV;", &tokens, &statement) == 0,
        "SELECT all students should parse successfully");
    ASSERT_STATEMENT_TYPE(STATEMENT_SELECT_STUDENT_ALL, statement.type);

    free_statement(&statement);
    free_token_list(&tokens);
}

static void test_parse_select_student_by_id_statement(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_and_parse(
            "SELECT * FROM STUDENT_CSV WHERE id = 302;",
            &tokens,
            &statement) == 0,
        "SELECT student by id should parse successfully");
    ASSERT_STATEMENT_TYPE(STATEMENT_SELECT_STUDENT_BY_ID, statement.type);
    ASSERT_TRUE(
        statement.data.select_student_by_id.id == 302,
        "select student id should be 302");

    free_statement(&statement);
    free_token_list(&tokens);
}

static void test_parse_select_entry_log_by_id_statement(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_and_parse(
            "SELECT * FROM ENTRY_LOG_BIN WHERE id = 302;",
            &tokens,
            &statement) == 0,
        "SELECT entry log by id should parse successfully");
    ASSERT_STATEMENT_TYPE(STATEMENT_SELECT_ENTRY_LOG_BY_ID, statement.type);
    ASSERT_TRUE(
        statement.data.select_entry_log_by_id.id == 302,
        "select entry log id should be 302");

    free_statement(&statement);
    free_token_list(&tokens);
}

static void test_parse_rejects_select_column_list(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql("SELECT id FROM STUDENT_CSV;", &tokens) == 0,
        "tokenizer should accept SELECT id input");
    ASSERT_TRUE(
        parse_statement(&tokens, &statement) != 0,
        "parser should reject SELECT column list");

    free_token_list(&tokens);
}

static void test_parse_rejects_where_on_non_id_column(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql("SELECT * FROM STUDENT_CSV WHERE name = 302;", &tokens) == 0,
        "tokenizer should accept WHERE name input");
    ASSERT_TRUE(
        parse_statement(&tokens, &statement) != 0,
        "parser should reject WHERE on columns other than id");

    free_token_list(&tokens);
}

static void test_parse_rejects_unknown_table_name(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql("SELECT * FROM UNKNOWN_TABLE;", &tokens) == 0,
        "tokenizer should accept unknown table tokenization");
    ASSERT_TRUE(
        parse_statement(&tokens, &statement) != 0,
        "parser should reject unknown tables");

    free_token_list(&tokens);
}

static void test_parse_rejects_values_count_mismatch(void)
{
    TokenList tokens;
    Statement statement;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql("INSERT INTO STUDENT_CSV VALUES (302, 'Kim');", &tokens) == 0,
        "tokenizer should accept incomplete VALUES tokenization");
    ASSERT_TRUE(
        parse_statement(&tokens, &statement) != 0,
        "parser should reject missing class value");

    free_token_list(&tokens);
}

int main(void)
{
    RUN_TEST(test_parse_insert_student_statement);
    RUN_TEST(test_parse_insert_entry_log_statement);
    RUN_TEST(test_parse_select_student_all_statement);
    RUN_TEST(test_parse_select_student_by_id_statement);
    RUN_TEST(test_parse_select_entry_log_by_id_statement);
    RUN_TEST(test_parse_rejects_select_column_list);
    RUN_TEST(test_parse_rejects_where_on_non_id_column);
    RUN_TEST(test_parse_rejects_unknown_table_name);
    RUN_TEST(test_parse_rejects_values_count_mismatch);

    printf("parser tests passed (%d)\n", tests_run);
    return 0;
}
