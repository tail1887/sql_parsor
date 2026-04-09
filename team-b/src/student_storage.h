#ifndef STUDENT_STORAGE_H
#define STUDENT_STORAGE_H

#include <stddef.h>

#define STUDENT_CSV_HEADER "id,name,class,authorization"

/*
 * storage 레이어는 "파일에 저장되는 학생 row"만 알고 있어야 한다.
 * SQL 문법이나 AST는 모른 채, 순수하게 CSV row 단위로 읽고 쓰는 역할만 맡는다.
 */
typedef struct {
    int id;
    char *name;
    int class_number;
    char authorization;
} StudentRecord;

typedef struct {
    StudentRecord *items;
    size_t count;
} StudentRecordList;

typedef enum {
    STUDENT_STORAGE_OK,
    STUDENT_STORAGE_IO_ERROR,
    STUDENT_STORAGE_INVALID_NAME,
    STUDENT_STORAGE_DUPLICATE_ID,
    STUDENT_STORAGE_INVALID_FILE
} StudentStorageStatus;

StudentStorageStatus ensure_student_csv_exists(const char *csv_path);
StudentStorageStatus append_student_record(const char *csv_path, const StudentRecord *record);
StudentStorageStatus read_all_student_records(
    const char *csv_path,
    StudentRecordList *out_list);
StudentStorageStatus find_student_record_by_id(
    const char *csv_path,
    int id,
    StudentRecord *out_record,
    int *out_found);
void free_student_record(StudentRecord *record);
void free_student_record_list(StudentRecordList *list);

#endif
