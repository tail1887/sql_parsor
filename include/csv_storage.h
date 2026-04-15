#ifndef CSV_STORAGE_H
#define CSV_STORAGE_H

#include "ast.h"

#include <stddef.h>

typedef struct {
    char **headers;
    size_t header_count;
    char ***rows;
    size_t row_count;
} CsvTable;

/* data/<table>.csv 전체를 읽어 헤더+행을 메모리에 적재한다. */
int csv_storage_read_table(const char *table, CsvTable **out);

/* 헤더 + 지정한 0-based 데이터 행 하나만 읽는다. 없으면 row_count=0 으로 반환한다. */
int csv_storage_read_table_row(const char *table, size_t row_index, CsvTable **out);

/* INSERT AST 값들을 CSV 한 행으로 직렬화해 파일 끝에 append 한다. */
int csv_storage_append_insert_row(const char *table, const SqlValue *values, size_t value_count);

/* 헤더 행만 읽어 컬럼 개수를 센다. */
int csv_storage_column_count(const char *table, size_t *out_count);

/* 헤더 다음 데이터 행 개수(빈 줄 제외). WEEK7: append 후 row_ref 로 사용. */
int csv_storage_data_row_count(const char *table, size_t *out_count);

void csv_storage_free_table(CsvTable *table);

#endif
