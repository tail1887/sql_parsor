#include "sql_file_reader.h"
#include "student_storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void remove_if_exists(const char *path)
{
    remove(path);
}

static void reset_test_directory(const char *directory_path, const char *csv_path)
{
    remove_if_exists(csv_path);
    rmdir(directory_path);
}

static void assert_status_equals(
    const char *test_name,
    StudentStorageStatus expected,
    StudentStorageStatus actual)
{
    if (expected != actual) {
        fprintf(
            stderr,
            "%s: expected status %d but got %d\n",
            test_name,
            (int)expected,
            (int)actual);
        exit(1);
    }
}

#define TEST_DIRECTORY "/tmp/sql_processor_student_storage_test"
#define TEST_CSV_PATH "/tmp/sql_processor_student_storage_test/student.csv"

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

#define ASSERT_STATUS(expected, actual) \
    do { \
        assert_status_equals(__func__, expected, actual); \
    } while (0)

#define RUN_TEST(test_function) \
    do { \
        test_function(); \
        tests_run += 1; \
    } while (0)

static void test_ensure_student_csv_creates_header_when_file_is_missing(void)
{
    char *contents;

    contents = NULL;
    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);

    ASSERT_STATUS(
        STUDENT_STORAGE_OK,
        ensure_student_csv_exists(TEST_CSV_PATH));
    ASSERT_TRUE(access(TEST_CSV_PATH, F_OK) == 0, "student.csv should be created");
    ASSERT_TRUE(access(TEST_DIRECTORY, F_OK) == 0, "parent directory should be created");
    ASSERT_TRUE(
        read_text_file(TEST_CSV_PATH, &contents) == 0,
        "created CSV should be readable");
    ASSERT_STRING_EQ("id,name,class,authorization\n", contents);

    free(contents);
    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);
}

static void test_append_and_read_all_student_records(void)
{
    StudentRecord first_record;
    StudentRecord second_record;
    StudentRecordList records;

    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);

    first_record.id = 302;
    first_record.name = "Kim";
    first_record.class_number = 302;
    first_record.authorization = 'T';

    second_record.id = 303;
    second_record.name = "Lee";
    second_record.class_number = 303;
    second_record.authorization = 'F';

    ASSERT_STATUS(
        STUDENT_STORAGE_OK,
        append_student_record(TEST_CSV_PATH, &first_record));
    ASSERT_STATUS(
        STUDENT_STORAGE_OK,
        append_student_record(TEST_CSV_PATH, &second_record));

    ASSERT_STATUS(
        STUDENT_STORAGE_OK,
        read_all_student_records(TEST_CSV_PATH, &records));
    ASSERT_TRUE(records.count == 2U, "two student rows should be returned");
    ASSERT_TRUE(records.items[0].id == 302, "first row id should be 302");
    ASSERT_STRING_EQ("Kim", records.items[0].name);
    ASSERT_TRUE(records.items[0].class_number == 302, "first row class should be 302");
    ASSERT_TRUE(records.items[0].authorization == 'T', "first row authorization should be T");
    ASSERT_TRUE(records.items[1].id == 303, "second row id should be 303");
    ASSERT_STRING_EQ("Lee", records.items[1].name);
    ASSERT_TRUE(records.items[1].authorization == 'F', "second row authorization should be F");

    free_student_record_list(&records);
    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);
}

static void test_append_student_record_rejects_duplicate_id(void)
{
    StudentRecord first_record;
    StudentRecord duplicate_record;

    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);

    first_record.id = 302;
    first_record.name = "Kim";
    first_record.class_number = 302;
    first_record.authorization = 'T';

    duplicate_record.id = 302;
    duplicate_record.name = "Park";
    duplicate_record.class_number = 301;
    duplicate_record.authorization = 'F';

    ASSERT_STATUS(
        STUDENT_STORAGE_OK,
        append_student_record(TEST_CSV_PATH, &first_record));
    ASSERT_STATUS(
        STUDENT_STORAGE_DUPLICATE_ID,
        append_student_record(TEST_CSV_PATH, &duplicate_record));

    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);
}

static void test_find_student_record_by_id_returns_matching_row(void)
{
    StudentRecord source_record;
    StudentRecord found_record;
    StudentStorageStatus status;
    int found;

    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);

    source_record.id = 100;
    source_record.name = "Coach";
    source_record.class_number = 100;
    source_record.authorization = 'T';

    ASSERT_STATUS(
        STUDENT_STORAGE_OK,
        append_student_record(TEST_CSV_PATH, &source_record));

    found_record.name = NULL;
    found = 0;
    status = find_student_record_by_id(TEST_CSV_PATH, 100, &found_record, &found);
    ASSERT_STATUS(STUDENT_STORAGE_OK, status);
    ASSERT_TRUE(found, "matching id should be found");
    ASSERT_TRUE(found_record.id == 100, "found id should be 100");
    ASSERT_STRING_EQ("Coach", found_record.name);
    ASSERT_TRUE(found_record.class_number == 100, "found class should be 100");
    ASSERT_TRUE(found_record.authorization == 'T', "found authorization should be T");

    free_student_record(&found_record);
    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);
}

static void test_append_student_record_rejects_invalid_name(void)
{
    StudentRecord invalid_record;

    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);

    invalid_record.id = 400;
    invalid_record.name = "Kim Lee";
    invalid_record.class_number = 302;
    invalid_record.authorization = 'T';

    ASSERT_STATUS(
        STUDENT_STORAGE_INVALID_NAME,
        append_student_record(TEST_CSV_PATH, &invalid_record));

    reset_test_directory(TEST_DIRECTORY, TEST_CSV_PATH);
}

int main(void)
{
    RUN_TEST(test_ensure_student_csv_creates_header_when_file_is_missing);
    RUN_TEST(test_append_and_read_all_student_records);
    RUN_TEST(test_append_student_record_rejects_duplicate_id);
    RUN_TEST(test_find_student_record_by_id_returns_matching_row);
    RUN_TEST(test_append_student_record_rejects_invalid_name);

    printf("student storage tests passed (%d)\n", tests_run);
    return 0;
}
