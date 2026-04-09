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

/* INSERT AST 값들을 CSV 한 행으로 직렬화해 파일 끝에 append 한다. */
int csv_storage_append_insert_row(const char *table, const SqlValue *values, size_t value_count);

void csv_storage_free_table(CsvTable *table);

#endif
