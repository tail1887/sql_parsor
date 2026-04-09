#include "sql_file_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
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

static int write_text_file(const char *path, const char *contents)
{
    FILE *file;

    file = fopen(path, "wb");
    if (file == NULL) {
        return 1;
    }

    if (fputs(contents, file) == EOF) {
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}

static void reset_work_directory(void)
{
    remove("/tmp/sql_processor_step6_test/query.sql");
    remove("/tmp/sql_processor_step6_test/stdout.txt");
    remove("/tmp/sql_processor_step6_test/stderr.txt");
    remove("/tmp/sql_processor_step6_test/data/entry_log.bin");
    remove("/tmp/sql_processor_step6_test/data/student.csv");
    rmdir("/tmp/sql_processor_step6_test/data");
    rmdir("/tmp/sql_processor_step6_test");
}

static void prepare_work_directory(void)
{
    reset_work_directory();

    if (mkdir("/tmp/sql_processor_step6_test", 0777) != 0) {
        fail_test(__func__, "failed to create temp work directory");
    }
}

static void run_cli_and_capture(
    const char *sql_text,
    char **out_stdout,
    char **out_stderr,
    char **out_student_csv,
    off_t *out_entry_log_size,
    int *out_exit_code)
{
    char current_directory[512];
    char command[2048];
    struct stat file_stat;

    *out_stdout = NULL;
    *out_stderr = NULL;
    *out_student_csv = NULL;
    *out_entry_log_size = -1;
    *out_exit_code = 1;

    prepare_work_directory();

    if (write_text_file("/tmp/sql_processor_step6_test/query.sql", sql_text) != 0) {
        fail_test(__func__, "failed to write SQL input file");
    }

    if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
        fail_test(__func__, "failed to get current directory");
    }

    snprintf(
        command,
        sizeof(command),
        "cd /tmp/sql_processor_step6_test && %s/sql_processor query.sql > stdout.txt 2> stderr.txt",
        current_directory);
    *out_exit_code = system(command);

    if (read_text_file("/tmp/sql_processor_step6_test/stdout.txt", out_stdout) != 0) {
        fail_test(__func__, "failed to read captured stdout");
    }

    if (read_text_file("/tmp/sql_processor_step6_test/stderr.txt", out_stderr) != 0) {
        fail_test(__func__, "failed to read captured stderr");
    }

    if (access("/tmp/sql_processor_step6_test/data/student.csv", F_OK) == 0) {
        if (read_text_file("/tmp/sql_processor_step6_test/data/student.csv", out_student_csv) != 0) {
            fail_test(__func__, "failed to read generated student.csv");
        }
    }

    if (stat("/tmp/sql_processor_step6_test/data/entry_log.bin", &file_stat) == 0) {
        *out_entry_log_size = file_stat.st_size;
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

static void test_cli_student_select_all_and_entry_log_select_in_one_file(void)
{
    char *stdout_text;
    char *stderr_text;
    char *student_csv_text;
    off_t entry_log_size;
    int exit_code;

    stdout_text = NULL;
    stderr_text = NULL;
    student_csv_text = NULL;

    run_cli_and_capture(
        "INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);\n"
        "INSERT INTO STUDENT_CSV VALUES (303, 'Lee', 303);\n"
        "INSERT INTO STUDENT_CSV VALUES (100, 'Coach', 100);\n"
        "SELECT * FROM STUDENT_CSV;\n"
        "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 302);\n"
        "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 18:30:00', 302);\n"
        "SELECT * FROM ENTRY_LOG_BIN WHERE id = 302;\n",
        &stdout_text,
        &stderr_text,
        &student_csv_text,
        &entry_log_size,
        &exit_code);

    ASSERT_TRUE(exit_code == 0, "CLI command should succeed");
    ASSERT_STRING_EQ(
        "id,name,class,authorization\n"
        "302,Kim,302,T\n"
        "303,Lee,303,F\n"
        "100,Coach,100,T\n"
        "entered_at,id\n"
        "2026-04-08 09:00:00,302\n"
        "2026-04-08 18:30:00,302\n",
        stdout_text);
    ASSERT_STRING_EQ("", stderr_text);
    ASSERT_STRING_EQ(
        "id,name,class,authorization\n"
        "302,Kim,302,T\n"
        "303,Lee,303,F\n"
        "100,Coach,100,T\n",
        student_csv_text);
    ASSERT_TRUE(entry_log_size == 24, "two binary rows should occupy 24 bytes");

    free(stdout_text);
    free(stderr_text);
    free(student_csv_text);
    reset_work_directory();
}

static void test_cli_entry_log_insert_rejects_unauthorized_student(void)
{
    char *stdout_text;
    char *stderr_text;
    char *student_csv_text;
    off_t entry_log_size;
    int exit_code;

    stdout_text = NULL;
    stderr_text = NULL;
    student_csv_text = NULL;

    run_cli_and_capture(
        "INSERT INTO STUDENT_CSV VALUES (303, 'Lee', 303);\n"
        "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 303);\n",
        &stdout_text,
        &stderr_text,
        &student_csv_text,
        &entry_log_size,
        &exit_code);

    ASSERT_TRUE(exit_code != 0, "CLI command should fail for unauthorized student");
    ASSERT_STRING_EQ("", stdout_text);
    ASSERT_STRING_EQ(
        "failed to insert entry log: unauthorized student id 303\n",
        stderr_text);
    ASSERT_STRING_EQ(
        "id,name,class,authorization\n"
        "303,Lee,303,F\n",
        student_csv_text);
    ASSERT_TRUE(entry_log_size == -1, "binary file should not be created on unauthorized insert");

    free(stdout_text);
    free(stderr_text);
    free(student_csv_text);
    reset_work_directory();
}

static void test_cli_entry_log_insert_rejects_missing_student(void)
{
    char *stdout_text;
    char *stderr_text;
    char *student_csv_text;
    off_t entry_log_size;
    int exit_code;

    stdout_text = NULL;
    stderr_text = NULL;
    student_csv_text = NULL;

    run_cli_and_capture(
        "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 999);\n",
        &stdout_text,
        &stderr_text,
        &student_csv_text,
        &entry_log_size,
        &exit_code);

    ASSERT_TRUE(exit_code != 0, "CLI command should fail for missing student");
    ASSERT_STRING_EQ("", stdout_text);
    ASSERT_STRING_EQ(
        "failed to insert entry log: student id 999 not found\n",
        stderr_text);
    ASSERT_STRING_EQ("id,name,class,authorization\n", student_csv_text);
    ASSERT_TRUE(entry_log_size == -1, "binary file should not be created on missing student");

    free(stdout_text);
    free(stderr_text);
    free(student_csv_text);
    reset_work_directory();
}

static void test_cli_stops_after_middle_statement_error(void)
{
    char *stdout_text;
    char *stderr_text;
    char *student_csv_text;
    off_t entry_log_size;
    int exit_code;

    stdout_text = NULL;
    stderr_text = NULL;
    student_csv_text = NULL;

    run_cli_and_capture(
        "INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);\n"
        "INSERT INTO STUDENT_CSV VALUES (303, 'Lee', 303);\n"
        "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 302);\n"
        "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 10:00:00', 303);\n"
        "INSERT INTO STUDENT_CSV VALUES (100, 'Coach', 100);\n"
        "INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 11:00:00', 302);\n",
        &stdout_text,
        &stderr_text,
        &student_csv_text,
        &entry_log_size,
        &exit_code);

    ASSERT_TRUE(exit_code != 0, "CLI command should stop after middle statement error");
    ASSERT_STRING_EQ("", stdout_text);
    ASSERT_STRING_EQ(
        "failed to insert entry log: unauthorized student id 303\n",
        stderr_text);
    ASSERT_STRING_EQ(
        "id,name,class,authorization\n"
        "302,Kim,302,T\n"
        "303,Lee,303,F\n",
        student_csv_text);
    ASSERT_TRUE(entry_log_size == 12, "only the first successful entry log insert should remain");

    free(stdout_text);
    free(stderr_text);
    free(student_csv_text);
    reset_work_directory();
}

int main(void)
{
    RUN_TEST(test_cli_student_select_all_and_entry_log_select_in_one_file);
    RUN_TEST(test_cli_entry_log_insert_rejects_unauthorized_student);
    RUN_TEST(test_cli_entry_log_insert_rejects_missing_student);
    RUN_TEST(test_cli_stops_after_middle_statement_error);

    printf("step6 functional tests passed (%d)\n", tests_run);
    return 0;
}
