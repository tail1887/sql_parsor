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

static void assert_token_equals(
    const char *test_name,
    const TokenList *tokens,
    size_t index,
    TokenType expected_type,
    const char *expected_text)
{
    if (index >= tokens->count) {
        fprintf(stderr, "%s: token index %lu is out of range\n", test_name, (unsigned long)index);
        exit(1);
    }

    if (tokens->items[index].type != expected_type) {
        fprintf(
            stderr,
            "%s: expected token type %s but got %s at index %lu\n",
            test_name,
            token_type_name(expected_type),
            token_type_name(tokens->items[index].type),
            (unsigned long)index);
        exit(1);
    }

    assert_string_equals(test_name, expected_text, tokens->items[index].text);
}

#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            fail_test(__func__, message); \
        } \
    } while (0)

#define ASSERT_TOKEN(tokens, index, type, text) \
    do { \
        assert_token_equals(__func__, tokens, index, type, text); \
    } while (0)

#define RUN_TEST(test_function) \
    do { \
        test_function(); \
        tests_run += 1; \
    } while (0)

static void test_tokenize_select_where_statement(void)
{
    TokenList tokens;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql("SELECT * FROM STUDENT_CSV WHERE id = 302;", &tokens) == 0,
        "SELECT statement should tokenize successfully");
    ASSERT_TRUE(tokens.count == 9U, "SELECT statement should produce 9 tokens");
    ASSERT_TOKEN(&tokens, 0U, TOKEN_SELECT, "SELECT");
    ASSERT_TOKEN(&tokens, 1U, TOKEN_STAR, "*");
    ASSERT_TOKEN(&tokens, 2U, TOKEN_FROM, "FROM");
    ASSERT_TOKEN(&tokens, 3U, TOKEN_IDENTIFIER, "STUDENT_CSV");
    ASSERT_TOKEN(&tokens, 4U, TOKEN_WHERE, "WHERE");
    ASSERT_TOKEN(&tokens, 5U, TOKEN_IDENTIFIER, "id");
    ASSERT_TOKEN(&tokens, 6U, TOKEN_EQUAL, "=");
    ASSERT_TOKEN(&tokens, 7U, TOKEN_INTEGER, "302");
    ASSERT_TOKEN(&tokens, 8U, TOKEN_SEMICOLON, ";");

    free_token_list(&tokens);
}

static void test_tokenize_insert_student_statement(void)
{
    TokenList tokens;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql("INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);", &tokens) == 0,
        "student INSERT should tokenize successfully");
    ASSERT_TRUE(tokens.count == 12U, "student INSERT should produce 12 tokens");
    ASSERT_TOKEN(&tokens, 0U, TOKEN_INSERT, "INSERT");
    ASSERT_TOKEN(&tokens, 1U, TOKEN_INTO, "INTO");
    ASSERT_TOKEN(&tokens, 2U, TOKEN_IDENTIFIER, "STUDENT_CSV");
    ASSERT_TOKEN(&tokens, 3U, TOKEN_VALUES, "VALUES");
    ASSERT_TOKEN(&tokens, 4U, TOKEN_LPAREN, "(");
    ASSERT_TOKEN(&tokens, 5U, TOKEN_INTEGER, "302");
    ASSERT_TOKEN(&tokens, 6U, TOKEN_COMMA, ",");
    ASSERT_TOKEN(&tokens, 7U, TOKEN_STRING, "Kim");
    ASSERT_TOKEN(&tokens, 8U, TOKEN_COMMA, ",");
    ASSERT_TOKEN(&tokens, 9U, TOKEN_INTEGER, "302");
    ASSERT_TOKEN(&tokens, 10U, TOKEN_RPAREN, ")");
    ASSERT_TOKEN(&tokens, 11U, TOKEN_SEMICOLON, ";");

    free_token_list(&tokens);
}

static void test_tokenize_insert_entry_log_statement_with_datetime_string(void)
{
    TokenList tokens;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql(
            "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 302);",
            &tokens) == 0,
        "entry log INSERT should tokenize successfully");
    ASSERT_TRUE(tokens.count == 10U, "entry log INSERT should produce 10 tokens");
    ASSERT_TOKEN(&tokens, 0U, TOKEN_INSERT, "INSERT");
    ASSERT_TOKEN(&tokens, 1U, TOKEN_INTO, "INTO");
    ASSERT_TOKEN(&tokens, 2U, TOKEN_IDENTIFIER, "ENTRY_LOG_BIN");
    ASSERT_TOKEN(&tokens, 3U, TOKEN_VALUES, "VALUES");
    ASSERT_TOKEN(&tokens, 4U, TOKEN_LPAREN, "(");
    ASSERT_TOKEN(&tokens, 5U, TOKEN_STRING, "2026-04-08 09:00:00");
    ASSERT_TOKEN(&tokens, 6U, TOKEN_COMMA, ",");
    ASSERT_TOKEN(&tokens, 7U, TOKEN_INTEGER, "302");
    ASSERT_TOKEN(&tokens, 8U, TOKEN_RPAREN, ")");
    ASSERT_TOKEN(&tokens, 9U, TOKEN_SEMICOLON, ";");

    free_token_list(&tokens);
}

static void test_tokenize_skips_spaces_and_newlines(void)
{
    TokenList tokens;
    const char *sql =
        "\n"
        "  SELECT   *  FROM   STUDENT_CSV   ;";

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql(sql, &tokens) == 0,
        "tokenizer should ignore extra whitespace");
    ASSERT_TRUE(tokens.count == 5U, "whitespace should not create extra tokens");
    ASSERT_TOKEN(&tokens, 0U, TOKEN_SELECT, "SELECT");
    ASSERT_TOKEN(&tokens, 1U, TOKEN_STAR, "*");
    ASSERT_TOKEN(&tokens, 2U, TOKEN_FROM, "FROM");
    ASSERT_TOKEN(&tokens, 3U, TOKEN_IDENTIFIER, "STUDENT_CSV");
    ASSERT_TOKEN(&tokens, 4U, TOKEN_SEMICOLON, ";");

    free_token_list(&tokens);
}

static void test_tokenize_rejects_unterminated_string(void)
{
    TokenList tokens;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql("INSERT INTO STUDENT_CSV VALUES (302, 'Kim, 302);", &tokens) != 0,
        "unterminated string should fail");
    ASSERT_TRUE(tokens.count == 0U, "failed tokenization should not return tokens");
}

static void test_tokenize_rejects_unsupported_character(void)
{
    TokenList tokens;

    tokens.items = NULL;
    tokens.count = 0U;

    ASSERT_TRUE(
        tokenize_sql("SELECT # FROM STUDENT_CSV;", &tokens) != 0,
        "unsupported character should fail");
    ASSERT_TRUE(tokens.count == 0U, "failed tokenization should not return tokens");
}

int main(void)
{
    RUN_TEST(test_tokenize_select_where_statement);
    RUN_TEST(test_tokenize_insert_student_statement);
    RUN_TEST(test_tokenize_insert_entry_log_statement_with_datetime_string);
    RUN_TEST(test_tokenize_skips_spaces_and_newlines);
    RUN_TEST(test_tokenize_rejects_unterminated_string);
    RUN_TEST(test_tokenize_rejects_unsupported_character);

    printf("tokenizer tests passed (%d)\n", tests_run);
    return 0;
}
