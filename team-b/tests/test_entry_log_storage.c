#include "entry_log_storage.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static int tests_run = 0;

static void fail_test(const char *test_name, const char *message)
{
    fprintf(stderr, "%s: %s\n", test_name, message);
    exit(1);
}

static void assert_status_equals(
    const char *test_name,
    EntryLogStorageStatus expected,
    EntryLogStorageStatus actual)
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

static void remove_if_exists(const char *path)
{
    remove(path);
}

static void reset_test_directory(const char *directory_path, const char *bin_path)
{
    remove_if_exists(bin_path);
    rmdir(directory_path);
}

#define TEST_DIRECTORY "/tmp/sql_processor_entry_log_storage_test"
#define TEST_BIN_PATH "/tmp/sql_processor_entry_log_storage_test/entry_log.bin"

#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            fail_test(__func__, message); \
        } \
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

static void test_ensure_entry_log_bin_creates_empty_file(void)
{
    struct stat file_stat;

    reset_test_directory(TEST_DIRECTORY, TEST_BIN_PATH);

    ASSERT_STATUS(
        ENTRY_LOG_STORAGE_OK,
        ensure_entry_log_bin_exists(TEST_BIN_PATH));
    ASSERT_TRUE(access(TEST_BIN_PATH, F_OK) == 0, "entry_log.bin should exist");
    ASSERT_TRUE(access(TEST_DIRECTORY, F_OK) == 0, "parent directory should exist");
    ASSERT_TRUE(stat(TEST_BIN_PATH, &file_stat) == 0, "entry_log.bin stat should succeed");
    ASSERT_TRUE(file_stat.st_size == 0, "new binary file should be empty");

    reset_test_directory(TEST_DIRECTORY, TEST_BIN_PATH);
}

static void test_append_and_read_entry_log_records_by_id(void)
{
    EntryLogRecord first_record;
    EntryLogRecord second_record;
    EntryLogRecord third_record;
    EntryLogRecordList list;

    reset_test_directory(TEST_DIRECTORY, TEST_BIN_PATH);

    first_record.entered_at = 1775638800LL;
    first_record.id = 302;
    second_record.entered_at = 1775642400LL;
    second_record.id = 303;
    third_record.entered_at = 1775673000LL;
    third_record.id = 302;

    ASSERT_STATUS(
        ENTRY_LOG_STORAGE_OK,
        append_entry_log_record(TEST_BIN_PATH, &first_record));
    ASSERT_STATUS(
        ENTRY_LOG_STORAGE_OK,
        append_entry_log_record(TEST_BIN_PATH, &second_record));
    ASSERT_STATUS(
        ENTRY_LOG_STORAGE_OK,
        append_entry_log_record(TEST_BIN_PATH, &third_record));

    ASSERT_STATUS(
        ENTRY_LOG_STORAGE_OK,
        read_entry_log_records_by_id(TEST_BIN_PATH, 302, &list));
    ASSERT_TRUE(list.count == 2U, "two matching records should be returned");
    ASSERT_TRUE(list.items[0].entered_at == 1775638800LL, "first timestamp should match");
    ASSERT_TRUE(list.items[0].id == 302, "first id should match");
    ASSERT_TRUE(list.items[1].entered_at == 1775673000LL, "second timestamp should match");
    ASSERT_TRUE(list.items[1].id == 302, "second id should match");

    free_entry_log_record_list(&list);
    reset_test_directory(TEST_DIRECTORY, TEST_BIN_PATH);
}

static void test_binary_record_size_is_twelve_bytes_per_row(void)
{
    EntryLogRecord first_record;
    EntryLogRecord second_record;
    struct stat file_stat;

    reset_test_directory(TEST_DIRECTORY, TEST_BIN_PATH);

    first_record.entered_at = 1775638800LL;
    first_record.id = 302;
    second_record.entered_at = 1775673000LL;
    second_record.id = 302;

    ASSERT_STATUS(
        ENTRY_LOG_STORAGE_OK,
        append_entry_log_record(TEST_BIN_PATH, &first_record));
    ASSERT_STATUS(
        ENTRY_LOG_STORAGE_OK,
        append_entry_log_record(TEST_BIN_PATH, &second_record));

    ASSERT_TRUE(stat(TEST_BIN_PATH, &file_stat) == 0, "binary file stat should succeed");
    ASSERT_TRUE(
        file_stat.st_size == (off_t)(ENTRY_LOG_RECORD_SIZE * 2U),
        "two rows should occupy exactly 24 bytes");

    reset_test_directory(TEST_DIRECTORY, TEST_BIN_PATH);
}

int main(void)
{
    RUN_TEST(test_ensure_entry_log_bin_creates_empty_file);
    RUN_TEST(test_append_and_read_entry_log_records_by_id);
    RUN_TEST(test_binary_record_size_is_twelve_bytes_per_row);

    printf("entry log storage tests passed (%d)\n", tests_run);
    return 0;
}
