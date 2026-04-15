#include "executor.h"

#include "csv_storage.h"
#include "week7/week7_index.h"

#include <stdlib.h>
#include <string.h>

/* 실행 시퀀스(다이어그램):
 *   MVP(6주차까지): docs/02-architecture.md §6.1 ~ 6.2
 *   WEEK7 B+ 인덱스 연계(설계·구현 시): docs/weeks/WEEK7/sequences.md
 */

int executor_execute_insert(const InsertStmt *stmt) {
    if (!stmt || !stmt->table) {
        return -1;
    }

    /* WEEK7: index hook — sequences.md */
    SqlValue *prep = NULL;
    size_t pn = 0;
    int64_t aid = 0;
    int pr = week7_prepare_insert_values(stmt, &prep, &pn, &aid);
    const SqlValue *vals = stmt->values;
    size_t vn = stmt->value_count;
    if (pr == 0) {
        vals = prep;
        vn = pn;
    } else if (pr != 1) {
        return -1;
    }

    int rc = csv_storage_append_insert_row(stmt->table, vals, vn);
    if (pr == 0) {
        week7_free_prepared(prep, pn);
    }
    if (rc != 0) {
        return rc;
    }

    size_t total = 0;
    if (csv_storage_data_row_count(stmt->table, &total) != 0) {
        return -1;
    }
    if (total == 0) {
        return -1;
    }
    size_t row_ix = total - 1;
    if (pr == 0) {
        if (week7_on_append_success(stmt->table, aid, row_ix) != 0) {
            return -1;
        }
    }
    return 0;
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

    /* WEEK7: indexed SELECT by primary key */
    if (stmt->has_where_id_eq) {
        if (week7_ensure_loaded(stmt->table) != 0) {
            return -1;
        }
        if (!week7_table_has_id_pk(stmt->table)) {
            return -1;
        }
        size_t ridx = 0;
        int hit = (week7_lookup_row(stmt->table, stmt->where_id_value, &ridx) == 0);

        CsvTable *t = NULL;
        if (hit) {
            if (csv_storage_read_table_row(stmt->table, ridx, &t) != 0 || !t) {
                return -1;
            }
        } else {
            if (csv_storage_read_table_row(stmt->table, (size_t)-1, &t) != 0 || !t) {
                return -1;
            }
        }
        if (hit && t->row_count != 1) {
            csv_storage_free_table(t);
            return -1;
        }
        if (!hit && t->row_count != 0) {
            csv_storage_free_table(t);
            return -1;
        }
        if (t->header_count == 0) {
            csv_storage_free_table(t);
            return -1;
        }

        int rc = 0;
        if (stmt->select_all) {
            for (size_t i = 0; i < t->header_count; i++) {
                if (i > 0 && fputc('\t', out) == EOF) {
                    rc = -1;
                    goto wdone;
                }
                if (fputs(t->headers[i], out) == EOF) {
                    rc = -1;
                    goto wdone;
                }
            }
            if (fputc('\n', out) == EOF) {
                rc = -1;
                goto wdone;
            }
            if (hit) {
                for (size_t c = 0; c < t->header_count; c++) {
                    if (c > 0 && fputc('\t', out) == EOF) {
                        rc = -1;
                        goto wdone;
                    }
                    if (fputs(t->rows[0][c], out) == EOF) {
                        rc = -1;
                        goto wdone;
                    }
                }
                if (fputc('\n', out) == EOF) {
                    rc = -1;
                    goto wdone;
                }
            }
        } else {
            int *idx = calloc(stmt->column_count, sizeof *idx);
            if (!idx) {
                rc = -1;
                goto wdone;
            }
            for (size_t i = 0; i < stmt->column_count; i++) {
                idx[i] = find_header_index(t->headers, t->header_count, stmt->columns[i]);
                if (idx[i] < 0) {
                    free(idx);
                    rc = -1;
                    goto wdone;
                }
            }
            for (size_t i = 0; i < stmt->column_count; i++) {
                if (i > 0 && fputc('\t', out) == EOF) {
                    free(idx);
                    rc = -1;
                    goto wdone;
                }
                if (fputs(stmt->columns[i], out) == EOF) {
                    free(idx);
                    rc = -1;
                    goto wdone;
                }
            }
            if (fputc('\n', out) == EOF) {
                free(idx);
                rc = -1;
                goto wdone;
            }
            if (hit) {
                for (size_t i = 0; i < stmt->column_count; i++) {
                    if (i > 0 && fputc('\t', out) == EOF) {
                        free(idx);
                        rc = -1;
                        goto wdone;
                    }
                    if (fputs(t->rows[0][idx[i]], out) == EOF) {
                        free(idx);
                        rc = -1;
                        goto wdone;
                    }
                }
                if (fputc('\n', out) == EOF) {
                    free(idx);
                    rc = -1;
                    goto wdone;
                }
            }
            free(idx);
        }
    wdone:
        csv_storage_free_table(t);
        return rc;
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
