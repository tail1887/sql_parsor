#include "executor.h"

#include "datetime_utils.h"
#include "entry_log_storage.h"
#include "student_storage.h"

#include <stdio.h>

/*
 * executor 레이어는 parser가 만든 AST를 보고
 * "어떤 storage 함수를 호출할지"와
 * "stdout / stderr에 무엇을 출력할지"를 결정한다.
 *
 * 즉:
 * - storage는 파일 포맷을 담당
 * - executor는 문장 의미 실행을 담당
 */

#define ENTRY_LOG_OUTPUT_HEADER "entered_at,id"

static char authorization_for_class_number(int class_number)
{
    /*
     * Step 4 명세의 권한 규칙은 매우 단순하다.
     * class 302, 100만 입장 권한 T이고 나머지는 모두 F다.
     */
    if (class_number == 302 || class_number == 100) {
        return 'T';
    }

    return 'F';
}

static void print_student_header(FILE *output_stream)
{
    fprintf(output_stream, "%s\n", STUDENT_CSV_HEADER);
}

static void print_student_record(FILE *output_stream, const StudentRecord *record)
{
    fprintf(
        output_stream,
        "%d,%s,%d,%c\n",
        record->id,
        record->name,
        record->class_number,
        record->authorization);
}

static void print_entry_log_header(FILE *output_stream)
{
    fprintf(output_stream, "%s\n", ENTRY_LOG_OUTPUT_HEADER);
}

static int find_student_for_entry_log(
    const char *student_csv_path,
    int id,
    StudentRecord *out_record,
    int *out_found,
    FILE *error_stream)
{
    StudentStorageStatus status;

    status = find_student_record_by_id(student_csv_path, id, out_record, out_found);
    if (status != STUDENT_STORAGE_OK) {
        fprintf(error_stream, "failed to read student records\n");
        return 1;
    }

    return 0;
}

static int print_entry_log_record(FILE *output_stream, const EntryLogRecord *record)
{
    char datetime_text[DATETIME_BUFFER_SIZE];

    if (format_unix_timestamp(record->entered_at, datetime_text, sizeof(datetime_text)) != 0) {
        return 1;
    }

    fprintf(output_stream, "%s,%d\n", datetime_text, (int)record->id);
    return 0;
}

static int execute_insert_student(
    const Statement *statement,
    const char *student_csv_path,
    FILE *error_stream)
{
    StudentRecord record;
    StudentStorageStatus status;

    /*
     * parser는 SQL 값을 AST로 해석만 해 둔다.
     * 실제 저장용 row를 구성하는 일은 executor가 맡는다.
     * 여기서 class -> authorization 규칙도 함께 적용한다.
     */
    record.id = statement->data.insert_student.id;
    record.name = statement->data.insert_student.name;
    record.class_number = statement->data.insert_student.class_number;
    record.authorization = authorization_for_class_number(record.class_number);

    status = append_student_record(student_csv_path, &record);
    if (status == STUDENT_STORAGE_OK) {
        return 0;
    }

    if (status == STUDENT_STORAGE_DUPLICATE_ID) {
        fprintf(error_stream, "failed to insert student: duplicate id %d\n", record.id);
        return 1;
    }

    if (status == STUDENT_STORAGE_INVALID_NAME) {
        fprintf(error_stream, "failed to insert student: invalid name\n");
        return 1;
    }

    fprintf(error_stream, "failed to insert student\n");
    return 1;
}

static int execute_select_student_all(
    const char *student_csv_path,
    FILE *output_stream,
    FILE *error_stream)
{
    StudentRecordList list;
    StudentStorageStatus status;
    size_t i;

    status = read_all_student_records(student_csv_path, &list);
    if (status != STUDENT_STORAGE_OK) {
        fprintf(error_stream, "failed to read student records\n");
        return 1;
    }

    if (list.count == 0U) {
        fprintf(output_stream, "no rows found\n");
        free_student_record_list(&list);
        return 0;
    }

    print_student_header(output_stream);
    for (i = 0U; i < list.count; i++) {
        print_student_record(output_stream, &list.items[i]);
    }

    free_student_record_list(&list);
    return 0;
}

static int execute_select_student_by_id(
    int id,
    const char *student_csv_path,
    FILE *output_stream,
    FILE *error_stream)
{
    StudentRecord record;
    StudentStorageStatus status;
    int found;

    /*
     * WHERE id = ... 는 row가 최대 1개만 나온다.
     * 그래서 전체 목록 대신 "한 건 찾기" 함수를 바로 호출한다.
     */
    status = find_student_record_by_id(student_csv_path, id, &record, &found);
    if (status != STUDENT_STORAGE_OK) {
        fprintf(error_stream, "failed to read student records\n");
        return 1;
    }

    if (!found) {
        fprintf(output_stream, "no rows found\n");
        return 0;
    }

    print_student_header(output_stream);
    print_student_record(output_stream, &record);
    free_student_record(&record);
    return 0;
}

static int execute_insert_entry_log(
    const Statement *statement,
    const char *student_csv_path,
    const char *entry_log_bin_path,
    FILE *error_stream)
{
    DateTimeParts parts;
    int64_t timestamp;
    EntryLogRecord record;
    StudentRecord student_record;
    int found;
    EntryLogStorageStatus status;

    student_record.name = NULL;
    found = 0;

    if (parse_datetime_string(statement->data.insert_entry_log.entered_at, &parts) != 0 ||
        datetime_parts_to_unix_timestamp(&parts, &timestamp) != 0) {
        fprintf(error_stream, "failed to insert entry log: invalid datetime\n");
        return 1;
    }

    if (find_student_for_entry_log(
            student_csv_path,
            statement->data.insert_entry_log.id,
            &student_record,
            &found,
            error_stream) != 0) {
        free_student_record(&student_record);
        return 1;
    }

    if (!found) {
        fprintf(error_stream, "failed to insert entry log: student id %d not found\n", statement->data.insert_entry_log.id);
        free_student_record(&student_record);
        return 1;
    }

    if (student_record.authorization != 'T') {
        fprintf(error_stream, "failed to insert entry log: unauthorized student id %d\n", statement->data.insert_entry_log.id);
        free_student_record(&student_record);
        return 1;
    }

    /* 입력은 문자열이지만 저장은 binary timestamp + id 조합으로 바꿔서 넣는다. */
    record.entered_at = timestamp;
    record.id = statement->data.insert_entry_log.id;

    status = append_entry_log_record(entry_log_bin_path, &record);
    free_student_record(&student_record);
    if (status != ENTRY_LOG_STORAGE_OK) {
        fprintf(error_stream, "failed to insert entry log\n");
        return 1;
    }

    return 0;
}

static int execute_select_entry_log_by_id(
    int id,
    const char *entry_log_bin_path,
    FILE *output_stream,
    FILE *error_stream)
{
    EntryLogRecordList list;
    EntryLogStorageStatus status;
    size_t i;

    status = read_entry_log_records_by_id(entry_log_bin_path, id, &list);
    if (status != ENTRY_LOG_STORAGE_OK) {
        fprintf(error_stream, "failed to read entry log records\n");
        return 1;
    }

    if (list.count == 0U) {
        fprintf(output_stream, "no rows found\n");
        free_entry_log_record_list(&list);
        return 0;
    }

    /* 저장은 timestamp여도 SELECT 결과는 사람이 읽는 datetime 문자열로 보여 준다. */
    print_entry_log_header(output_stream);
    for (i = 0U; i < list.count; i++) {
        if (print_entry_log_record(output_stream, &list.items[i]) != 0) {
            fprintf(error_stream, "failed to format entry log record\n");
            free_entry_log_record_list(&list);
            return 1;
        }
    }

    free_entry_log_record_list(&list);
    return 0;
}

int execute_statement(
    const Statement *statement,
    const char *student_csv_path,
    const char *entry_log_bin_path,
    FILE *output_stream,
    FILE *error_stream)
{
    if (statement == NULL ||
        student_csv_path == NULL ||
        entry_log_bin_path == NULL ||
        output_stream == NULL ||
        error_stream == NULL) {
        return 1;
    }

    switch (statement->type) {
    case STATEMENT_INSERT_STUDENT:
        return execute_insert_student(statement, student_csv_path, error_stream);

    case STATEMENT_SELECT_STUDENT_ALL:
        return execute_select_student_all(student_csv_path, output_stream, error_stream);

    case STATEMENT_SELECT_STUDENT_BY_ID:
        return execute_select_student_by_id(
            statement->data.select_student_by_id.id,
            student_csv_path,
            output_stream,
            error_stream);

    case STATEMENT_INSERT_ENTRY_LOG:
        return execute_insert_entry_log(
            statement,
            student_csv_path,
            entry_log_bin_path,
            error_stream);

    case STATEMENT_SELECT_ENTRY_LOG_BY_ID:
        return execute_select_entry_log_by_id(
            statement->data.select_entry_log_by_id.id,
            entry_log_bin_path,
            output_stream,
            error_stream);
    }

    fprintf(error_stream, "unknown statement type\n");
    return 1;
}
