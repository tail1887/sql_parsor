#include "executor.h"

#include "csv_storage.h"

#include <stdlib.h>
#include <string.h>

int executor_execute_insert(const InsertStmt *stmt) {
    if (!stmt || !stmt->table) {
        return -1;
    }
    return csv_storage_append_insert_row(stmt->table, stmt->values, stmt->value_count);
}

static int find_header_index(char **headers, size_t header_count, const char *name) {
    for (size_t i = 0; i < header_count; i++) {
        if (strcmp(headers[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int executor_execute_select(const SelectStmt *stmt, FILE *out) {
    if (!stmt || !stmt->table || !out) {
        return -1;
    }

    CsvTable *t = NULL;
    if (csv_storage_read_table(stmt->table, &t) != 0 || !t) {
        return -1;
    }

    int rc = 0;

    if (stmt->select_all) {
        for (size_t i = 0; i < t->header_count; i++) {
            if (i > 0 && fputc('\t', out) == EOF) {
                rc = -1;
                goto done;
            }
            if (fputs(t->headers[i], out) == EOF) {
                rc = -1;
                goto done;
            }
        }
        if (fputc('\n', out) == EOF) {
            rc = -1;
            goto done;
        }

        for (size_t r = 0; r < t->row_count; r++) {
            for (size_t c = 0; c < t->header_count; c++) {
                if (c > 0 && fputc('\t', out) == EOF) {
                    rc = -1;
                    goto done;
                }
                if (fputs(t->rows[r][c], out) == EOF) {
                    rc = -1;
                    goto done;
                }
            }
            if (fputc('\n', out) == EOF) {
                rc = -1;
                goto done;
            }
        }
    } else {
        int *idx = calloc(stmt->column_count, sizeof *idx);
        if (!idx) {
            rc = -1;
            goto done;
        }

        for (size_t i = 0; i < stmt->column_count; i++) {
            idx[i] = find_header_index(t->headers, t->header_count, stmt->columns[i]);
            if (idx[i] < 0) {
                free(idx);
                rc = -1;
                goto done;
            }
        }

        for (size_t i = 0; i < stmt->column_count; i++) {
            if (i > 0 && fputc('\t', out) == EOF) {
                free(idx);
                rc = -1;
                goto done;
            }
            if (fputs(stmt->columns[i], out) == EOF) {
                free(idx);
                rc = -1;
                goto done;
            }
        }
        if (fputc('\n', out) == EOF) {
            free(idx);
            rc = -1;
            goto done;
        }

        for (size_t r = 0; r < t->row_count; r++) {
            for (size_t i = 0; i < stmt->column_count; i++) {
                if (i > 0 && fputc('\t', out) == EOF) {
                    free(idx);
                    rc = -1;
                    goto done;
                }
                if (fputs(t->rows[r][idx[i]], out) == EOF) {
                    free(idx);
                    rc = -1;
                    goto done;
                }
            }
            if (fputc('\n', out) == EOF) {
                free(idx);
                rc = -1;
                goto done;
            }
        }

        free(idx);
    }

done:
    csv_storage_free_table(t);
    return rc;
}
