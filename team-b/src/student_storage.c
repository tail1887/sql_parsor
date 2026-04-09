#include "student_storage.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * Step 4의 student storage는 아래 일만 담당한다.
 * 1. student.csv 파일이 없으면 헤더를 가진 빈 파일을 만든다.
 * 2. row 1개를 CSV 한 줄로 append 한다.
 * 3. CSV 파일 전체를 다시 읽어 StudentRecord 배열로 복원한다.
 *
 * 중요한 점:
 * - storage는 SQL 문장을 모른다.
 * - tokenizer / parser / StatementType도 모른다.
 * - 그냥 "학생 row를 파일에 저장/조회하는 모듈"이다.
 */

static void initialize_student_record(StudentRecord *record)
{
    record->id = 0;
    record->name = NULL;
    record->class_number = 0;
    record->authorization = 'F';
}

static void initialize_student_record_list(StudentRecordList *list)
{
    list->items = NULL;
    list->count = 0U;
}

static int duplicate_text(const char *text, char **out_text)
{
    size_t length;
    char *copy;

    if (text == NULL || out_text == NULL) {
        return 1;
    }

    length = strlen(text);
    copy = (char *)malloc(length + 1U);
    if (copy == NULL) {
        return 1;
    }

    memcpy(copy, text, length + 1U);
    *out_text = copy;
    return 0;
}

static int ensure_parent_directory_exists(const char *file_path)
{
    const char *last_slash;
    size_t directory_length;
    char *directory_path;
    int mkdir_result;

    /*
     * file_path가 "data/student.csv" 라면
     * 마지막 '/' 앞부분인 "data"를 만들어야 한다.
     *
     * 경로에 '/'가 없으면 현재 디렉터리에 파일을 만드는 경우이므로
     * 따로 디렉터리를 만들 필요가 없다.
     */
    last_slash = strrchr(file_path, '/');
    if (last_slash == NULL) {
        return 0;
    }

    directory_length = (size_t)(last_slash - file_path);
    if (directory_length == 0U) {
        return 0;
    }

    directory_path = (char *)malloc(directory_length + 1U);
    if (directory_path == NULL) {
        return 1;
    }

    memcpy(directory_path, file_path, directory_length);
    directory_path[directory_length] = '\0';

    mkdir_result = mkdir(directory_path, 0777);
    free(directory_path);

    if (mkdir_result == 0 || errno == EEXIST) {
        return 0;
    }

    return 1;
}

static int parse_int_text(const char *text, int *out_value)
{
    long parsed_value;
    char *end_ptr;

    if (text == NULL || out_value == NULL) {
        return 1;
    }

    parsed_value = strtol(text, &end_ptr, 10);
    if (*text == '\0' || *end_ptr != '\0') {
        return 1;
    }

    if (parsed_value < INT_MIN || parsed_value > INT_MAX) {
        return 1;
    }

    *out_value = (int)parsed_value;
    return 0;
}

static int student_name_is_valid(const char *name)
{
    size_t i;

    /*
     * 이번 프로젝트의 CSV는 아주 단순하게 유지한다.
     * 그래서 name에는 공백, 쉼표, 줄바꿈을 허용하지 않는다.
     * 이 제약 덕분에 일반적인 CSV parser를 만들 필요가 없다.
     */
    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    for (i = 0U; name[i] != '\0'; i++) {
        if (isspace((unsigned char)name[i]) || name[i] == ',') {
            return 0;
        }
    }

    return 1;
}

static void trim_line_endings(char *text)
{
    size_t length;

    if (text == NULL) {
        return;
    }

    length = strlen(text);
    while (length > 0U &&
           (text[length - 1U] == '\n' || text[length - 1U] == '\r')) {
        text[length - 1U] = '\0';
        length -= 1U;
    }
}

static int ensure_list_capacity(StudentRecordList *list, size_t *capacity)
{
    StudentRecord *new_items;
    size_t new_capacity;

    if (list->count < *capacity) {
        return 0;
    }

    new_capacity = (*capacity == 0U) ? 8U : (*capacity * 2U);
    new_items = (StudentRecord *)realloc(list->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        return 1;
    }

    list->items = new_items;
    *capacity = new_capacity;
    return 0;
}

static StudentStorageStatus duplicate_student_record(
    const StudentRecord *source,
    StudentRecord *destination)
{
    initialize_student_record(destination);

    destination->id = source->id;
    destination->class_number = source->class_number;
    destination->authorization = source->authorization;

    if (duplicate_text(source->name, &destination->name) != 0) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    return STUDENT_STORAGE_OK;
}

static StudentStorageStatus parse_student_csv_line(char *line, StudentRecord *out_record)
{
    char *first_comma;
    char *second_comma;
    char *third_comma;
    char *id_text;
    char *name_text;
    char *class_text;
    char *authorization_text;

    if (line == NULL || out_record == NULL) {
        return STUDENT_STORAGE_INVALID_FILE;
    }

    initialize_student_record(out_record);

    /*
     * 지원하는 CSV 형식은 정확히 아래 4칸뿐이다.
     * id,name,class,authorization
     *
     * 따라서 첫 번째/두 번째/세 번째 쉼표 위치만 찾으면
     * 각 칸을 쉽게 잘라낼 수 있다.
     */
    first_comma = strchr(line, ',');
    if (first_comma == NULL) {
        return STUDENT_STORAGE_INVALID_FILE;
    }

    second_comma = strchr(first_comma + 1, ',');
    if (second_comma == NULL) {
        return STUDENT_STORAGE_INVALID_FILE;
    }

    third_comma = strchr(second_comma + 1, ',');
    if (third_comma == NULL) {
        return STUDENT_STORAGE_INVALID_FILE;
    }

    if (strchr(third_comma + 1, ',') != NULL) {
        return STUDENT_STORAGE_INVALID_FILE;
    }

    *first_comma = '\0';
    *second_comma = '\0';
    *third_comma = '\0';

    id_text = line;
    name_text = first_comma + 1;
    class_text = second_comma + 1;
    authorization_text = third_comma + 1;

    if (parse_int_text(id_text, &out_record->id) != 0) {
        return STUDENT_STORAGE_INVALID_FILE;
    }

    if (!student_name_is_valid(name_text)) {
        return STUDENT_STORAGE_INVALID_FILE;
    }

    if (duplicate_text(name_text, &out_record->name) != 0) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    if (parse_int_text(class_text, &out_record->class_number) != 0) {
        free_student_record(out_record);
        return STUDENT_STORAGE_INVALID_FILE;
    }

    if (authorization_text[0] == '\0' || authorization_text[1] != '\0') {
        free_student_record(out_record);
        return STUDENT_STORAGE_INVALID_FILE;
    }

    if (authorization_text[0] != 'T' && authorization_text[0] != 'F') {
        free_student_record(out_record);
        return STUDENT_STORAGE_INVALID_FILE;
    }

    out_record->authorization = authorization_text[0];
    return STUDENT_STORAGE_OK;
}

void free_student_record(StudentRecord *record)
{
    if (record == NULL) {
        return;
    }

    free(record->name);
    record->name = NULL;
}

void free_student_record_list(StudentRecordList *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }

    for (i = 0U; i < list->count; i++) {
        free_student_record(&list->items[i]);
    }

    free(list->items);
    list->items = NULL;
    list->count = 0U;
}

StudentStorageStatus ensure_student_csv_exists(const char *csv_path)
{
    FILE *file;

    if (csv_path == NULL) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    /*
     * 먼저 읽기 모드로 열어 보고 성공하면
     * 이미 파일이 있다는 뜻이므로 그대로 끝낸다.
     */
    file = fopen(csv_path, "rb");
    if (file != NULL) {
        fclose(file);
        return STUDENT_STORAGE_OK;
    }

    if (ensure_parent_directory_exists(csv_path) != 0) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    file = fopen(csv_path, "wb");
    if (file == NULL) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    /*
     * 빈 student.csv는 헤더 한 줄만 가진다.
     * 이후 append는 항상 헤더 아래에 데이터 row를 추가한다.
     */
    if (fprintf(file, "%s\n", STUDENT_CSV_HEADER) < 0) {
        fclose(file);
        return STUDENT_STORAGE_IO_ERROR;
    }

    fclose(file);
    return STUDENT_STORAGE_OK;
}

StudentStorageStatus append_student_record(const char *csv_path, const StudentRecord *record)
{
    FILE *file;
    StudentRecord duplicate_record;
    StudentStorageStatus status;
    int found;

    if (csv_path == NULL || record == NULL) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    if (!student_name_is_valid(record->name)) {
        return STUDENT_STORAGE_INVALID_NAME;
    }

    if (record->authorization != 'T' && record->authorization != 'F') {
        return STUDENT_STORAGE_INVALID_FILE;
    }

    status = ensure_student_csv_exists(csv_path);
    if (status != STUDENT_STORAGE_OK) {
        return status;
    }

    initialize_student_record(&duplicate_record);
    found = 0;

    /*
     * id는 PK처럼 다루므로 append 전에 반드시 한 번 검사한다.
     * 현재 단계에서는 파일 전체를 다시 읽어 같은 id가 있는지 확인하는
     * 가장 단순한 방법을 사용한다.
     */
    status = find_student_record_by_id(csv_path, record->id, &duplicate_record, &found);
    if (status != STUDENT_STORAGE_OK) {
        free_student_record(&duplicate_record);
        return status;
    }

    free_student_record(&duplicate_record);
    if (found) {
        return STUDENT_STORAGE_DUPLICATE_ID;
    }

    file = fopen(csv_path, "ab");
    if (file == NULL) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    if (fprintf(
            file,
            "%d,%s,%d,%c\n",
            record->id,
            record->name,
            record->class_number,
            record->authorization) < 0) {
        fclose(file);
        return STUDENT_STORAGE_IO_ERROR;
    }

    fclose(file);
    return STUDENT_STORAGE_OK;
}

StudentStorageStatus read_all_student_records(
    const char *csv_path,
    StudentRecordList *out_list)
{
    FILE *file;
    char line_buffer[512];
    StudentRecordList list;
    size_t capacity;

    if (csv_path == NULL || out_list == NULL) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    *out_list = (StudentRecordList){NULL, 0U};

    if (ensure_student_csv_exists(csv_path) != STUDENT_STORAGE_OK) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    file = fopen(csv_path, "rb");
    if (file == NULL) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    initialize_student_record_list(&list);
    capacity = 0U;

    if (fgets(line_buffer, sizeof(line_buffer), file) == NULL) {
        fclose(file);
        return STUDENT_STORAGE_INVALID_FILE;
    }

    trim_line_endings(line_buffer);
    if (strcmp(line_buffer, STUDENT_CSV_HEADER) != 0) {
        fclose(file);
        return STUDENT_STORAGE_INVALID_FILE;
    }

    while (fgets(line_buffer, sizeof(line_buffer), file) != NULL) {
        StudentRecord record;
        StudentStorageStatus status;

        if (strchr(line_buffer, '\n') == NULL && !feof(file)) {
            fclose(file);
            free_student_record_list(&list);
            return STUDENT_STORAGE_INVALID_FILE;
        }

        trim_line_endings(line_buffer);
        if (line_buffer[0] == '\0') {
            continue;
        }

        if (ensure_list_capacity(&list, &capacity) != 0) {
            fclose(file);
            free_student_record_list(&list);
            return STUDENT_STORAGE_IO_ERROR;
        }

        status = parse_student_csv_line(line_buffer, &record);
        if (status != STUDENT_STORAGE_OK) {
            fclose(file);
            free_student_record_list(&list);
            return status;
        }

        list.items[list.count] = record;
        list.count += 1U;
    }

    fclose(file);
    *out_list = list;
    return STUDENT_STORAGE_OK;
}

StudentStorageStatus find_student_record_by_id(
    const char *csv_path,
    int id,
    StudentRecord *out_record,
    int *out_found)
{
    StudentRecordList list;
    StudentStorageStatus status;
    size_t i;

    if (csv_path == NULL || out_record == NULL || out_found == NULL) {
        return STUDENT_STORAGE_IO_ERROR;
    }

    initialize_student_record(out_record);
    *out_found = 0;

    status = read_all_student_records(csv_path, &list);
    if (status != STUDENT_STORAGE_OK) {
        return status;
    }

    for (i = 0U; i < list.count; i++) {
        if (list.items[i].id == id) {
            status = duplicate_student_record(&list.items[i], out_record);
            free_student_record_list(&list);
            if (status != STUDENT_STORAGE_OK) {
                return status;
            }

            *out_found = 1;
            return STUDENT_STORAGE_OK;
        }
    }

    free_student_record_list(&list);
    return STUDENT_STORAGE_OK;
}
