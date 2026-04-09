#include "datetime_utils.h"

#include <stdint.h>
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

#define RUN_TEST(test_function) \
    do { \
        test_function(); \
        tests_run += 1; \
    } while (0)

static void test_parse_datetime_string_reads_all_fields(void)
{
    DateTimeParts parts;

    ASSERT_TRUE(
        parse_datetime_string("2026-04-08 09:00:00", &parts) == 0,
        "valid datetime should parse");
    ASSERT_TRUE(parts.year == 2026, "year should be 2026");
    ASSERT_TRUE(parts.month == 4, "month should be 4");
    ASSERT_TRUE(parts.day == 8, "day should be 8");
    ASSERT_TRUE(parts.hour == 9, "hour should be 9");
    ASSERT_TRUE(parts.minute == 0, "minute should be 0");
    ASSERT_TRUE(parts.second == 0, "second should be 0");
}

static void test_parse_datetime_string_rejects_wrong_separators(void)
{
    DateTimeParts parts;

    ASSERT_TRUE(
        parse_datetime_string("2026/04/08 09:00:00", &parts) != 0,
        "slashes should be rejected");
}

static void test_parse_datetime_string_rejects_invalid_calendar_date(void)
{
    DateTimeParts parts;

    ASSERT_TRUE(
        parse_datetime_string("2026-02-29 09:00:00", &parts) != 0,
        "invalid day should be rejected");
}

static void test_datetime_parts_to_unix_timestamp_returns_expected_value(void)
{
    DateTimeParts parts;
    int64_t timestamp;

    ASSERT_TRUE(
        parse_datetime_string("2026-04-08 09:00:00", &parts) == 0,
        "datetime should parse");
    ASSERT_TRUE(
        datetime_parts_to_unix_timestamp(&parts, &timestamp) == 0,
        "timestamp conversion should succeed");
    ASSERT_TRUE(timestamp == 1775638800LL, "timestamp should match expected unix time");
}

static void test_format_unix_timestamp_returns_datetime_text(void)
{
    char buffer[DATETIME_BUFFER_SIZE];

    ASSERT_TRUE(
        format_unix_timestamp(1775673000LL, buffer, sizeof(buffer)) == 0,
        "formatting should succeed");
    ASSERT_STRING_EQ("2026-04-08 18:30:00", buffer);
}

int main(void)
{
    RUN_TEST(test_parse_datetime_string_reads_all_fields);
    RUN_TEST(test_parse_datetime_string_rejects_wrong_separators);
    RUN_TEST(test_parse_datetime_string_rejects_invalid_calendar_date);
    RUN_TEST(test_datetime_parts_to_unix_timestamp_returns_expected_value);
    RUN_TEST(test_format_unix_timestamp_returns_datetime_text);

    printf("datetime utils tests passed (%d)\n", tests_run);
    return 0;
}
