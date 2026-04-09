#include "sql_file_reader.h"
#include "statement_splitter.h"

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

/* Git on Windows may checkout fixtures with CRLF; accept \r\n or \n as LF. */
static int strings_equal_lf_expected_crlf_ok(
    const char *expected_lf,
    const char *actual)
{
    const char *e = expected_lf;
    const char *a = actual;

    while (*e != '\0' && *a != '\0') {
        if (*e == '\n' && *a == '\r' && *(a + 1) == '\n') {
            e++;
            a += 2;
            continue;
        }
        if (*e != *a) {
            return 0;
        }
        e++;
        a++;
    }
    return *e == '\0' && *a == '\0';
}

static void assert_string_equals_allow_crlf_in_actual(
    const char *test_name,
    const char *expected_lf,
    const char *actual)
{
    if (!strings_equal_lf_expected_crlf_ok(expected_lf, actual)) {
        fprintf(
            stderr,
            "%s: expected [%s] but got [%s]\n",
            test_name,
            expected_lf,
            actual);
        exit(1);
    }
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

#define ASSERT_STRING_EQ_ALLOW_CRLF_IN_ACTUAL(expected_lf, actual) \
    do { \
        assert_string_equals_allow_crlf_in_actual( \
            __func__, \
            (expected_lf), \
            (actual)); \
    } while (0)

#define RUN_TEST(test_function) \
    do { \
        test_function(); \
        tests_run += 1; \
    } while (0)

static void test_read_text_file_reads_full_contents(void)
{
    char *contents;

    contents = NULL;

    ASSERT_TRUE(
        read_text_file("tests/fixtures/read_file_sample.sql", &contents) == 0,
        "read_text_file should succeed for fixture input");
    ASSERT_STRING_EQ_ALLOW_CRLF_IN_ACTUAL("SELECT * FROM STUDENT_CSV;\n", contents);

    free(contents);
}

static void test_split_sql_statements_single_statement(void)
{
    StatementList statements;

    statements.items = NULL;
    statements.count = 0U;

    ASSERT_TRUE(
        split_sql_statements("SELECT * FROM STUDENT_CSV;", &statements) == 0,
        "single statement should split successfully");
    ASSERT_TRUE(statements.count == 1U, "single statement should produce one item");
    ASSERT_STRING_EQ("SELECT * FROM STUDENT_CSV;", statements.items[0]);

    free_statement_list(&statements);
}

static void test_split_sql_statements_multiple_statements_preserve_order(void)
{
    const char *sql =
        "INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);\n"
        "SELECT * FROM STUDENT_CSV;\n"
        "SELECT * FROM STUDENT_CSV WHERE id = 302;";
    StatementList statements;

    statements.items = NULL;
    statements.count = 0U;

    ASSERT_TRUE(
        split_sql_statements(sql, &statements) == 0,
        "multiple statements should split successfully");
    ASSERT_TRUE(statements.count == 3U, "three statements should be returned");
    ASSERT_STRING_EQ(
        "INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);",
        statements.items[0]);
    ASSERT_STRING_EQ("SELECT * FROM STUDENT_CSV;", statements.items[1]);
    ASSERT_STRING_EQ(
        "SELECT * FROM STUDENT_CSV WHERE id = 302;",
        statements.items[2]);

    free_statement_list(&statements);
}

static void test_split_sql_statements_trim_leading_whitespace(void)
{
    const char *sql =
        "\n"
        "   INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);\n"
        "   ";
    StatementList statements;

    statements.items = NULL;
    statements.count = 0U;

    ASSERT_TRUE(
        split_sql_statements(sql, &statements) == 0,
        "splitter should ignore surrounding whitespace");
    ASSERT_TRUE(statements.count == 1U, "trimmed input should still produce one item");
    ASSERT_STRING_EQ(
        "INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);",
        statements.items[0]);

    free_statement_list(&statements);
}

int main(void)
{
    RUN_TEST(test_read_text_file_reads_full_contents);
    RUN_TEST(test_split_sql_statements_single_statement);
    RUN_TEST(test_split_sql_statements_multiple_statements_preserve_order);
    RUN_TEST(test_split_sql_statements_trim_leading_whitespace);

    printf("step1 tests passed (%d)\n", tests_run);
    return 0;
}
