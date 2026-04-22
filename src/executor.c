#include "executor.h"

#include "csv_storage.h"
#include "week7/week7_index.h"

#include <stdlib.h>
#include <string.h>

/* 실행 시퀀스(다이어그램):
 *   MVP(6주차까지): docs/02-architecture.md §6.1 ~ 6.2
 *   WEEK7 B+ 인덱스 연계(설계·구현 시): docs/weeks/WEEK7/sequences.md
 */

static char *dup_cstr(const char *text) {
    size_t n = 0;
    char *copy = NULL;

    if (!text) {
        text = "";
    }
    n = strlen(text);
    copy = malloc(n + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, n + 1);
    return copy;
}

static int set_result_message(SqlExecutionResult *result, const char *message) {
    char *copy = dup_cstr(message);
    if (!copy) {
        return -1;
    }
    result->message = copy;
    return 0;
}

static int set_select_message(SqlExecutionResult *result, size_t row_count) {
    char buffer[64];

    snprintf(buffer, sizeof buffer, "%zu %s selected", row_count, row_count == 1 ? "row" : "rows");
    return set_result_message(result, buffer);
}

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

static int load_where_table(const SelectStmt *stmt, CsvTable **out) {
    CsvTable *table = NULL;
    size_t row_index = 0;
    int hit = 0;

    if (week7_ensure_loaded(stmt->table) != 0) {
        return -1;
    }
    if (!week7_table_has_id_pk(stmt->table)) {
        return -1;
    }

    hit = (week7_lookup_row(stmt->table, stmt->where_id_value, &row_index) == 0);
    if (hit) {
        if (csv_storage_read_table_row(stmt->table, row_index, &table) != 0 || !table) {
            return -1;
        }
    } else {
        if (csv_storage_read_table_row(stmt->table, (size_t)-1, &table) != 0 || !table) {
            return -1;
        }
    }

    if (table->header_count == 0) {
        csv_storage_free_table(table);
        return -1;
    }
    if (hit && table->row_count != 1) {
        csv_storage_free_table(table);
        return -1;
    }
    if (!hit && table->row_count != 0) {
        csv_storage_free_table(table);
        return -1;
    }

    *out = table;
    return 0;
}

static int load_select_table(const SelectStmt *stmt, CsvTable **out) {
    if (stmt->has_where_id_eq) {
        return load_where_table(stmt, out);
    }
    return csv_storage_read_table(stmt->table, out);
}

static int build_projection(const CsvTable *table, const SelectStmt *stmt, SqlExecutionResult *out) {
    SqlExecutionResult tmp = {0};
    int *indices = NULL;
    size_t column_count = 0;

    if (!table || !stmt || !out || table->header_count == 0) {
        return -1;
    }

    tmp.statement_type = SQL_STATEMENT_SELECT;
    tmp.exit_code = 0;

    column_count = stmt->select_all ? table->header_count : stmt->column_count;
    tmp.column_count = column_count;

    if (column_count > 0) {
        tmp.columns = calloc(column_count, sizeof *tmp.columns);
        if (!tmp.columns) {
            return -1;
        }
    }

    if (!stmt->select_all && column_count > 0) {
        indices = calloc(column_count, sizeof *indices);
        if (!indices) {
            sql_execution_result_clear(&tmp);
            return -1;
        }
        for (size_t i = 0; i < column_count; i++) {
            indices[i] = find_header_index(table->headers, table->header_count, stmt->columns[i]);
            if (indices[i] < 0) {
                free(indices);
                sql_execution_result_clear(&tmp);
                return -1;
            }
        }
    }

    for (size_t i = 0; i < column_count; i++) {
        const char *name = stmt->select_all ? table->headers[i] : stmt->columns[i];
        tmp.columns[i] = dup_cstr(name);
        if (!tmp.columns[i]) {
            free(indices);
            sql_execution_result_clear(&tmp);
            return -1;
        }
    }

    tmp.row_count = table->row_count;
    if (tmp.row_count > 0) {
        tmp.rows = calloc(tmp.row_count, sizeof *tmp.rows);
        if (!tmp.rows) {
            free(indices);
            sql_execution_result_clear(&tmp);
            return -1;
        }
    }

    for (size_t r = 0; r < tmp.row_count; r++) {
        tmp.rows[r] = calloc(column_count, sizeof *tmp.rows[r]);
        if (!tmp.rows[r]) {
            free(indices);
            sql_execution_result_clear(&tmp);
            return -1;
        }
        for (size_t c = 0; c < column_count; c++) {
            size_t src_index = stmt->select_all ? c : (size_t)indices[c];
            tmp.rows[r][c] = dup_cstr(table->rows[r][src_index]);
            if (!tmp.rows[r][c]) {
                free(indices);
                sql_execution_result_clear(&tmp);
                return -1;
            }
        }
    }

    free(indices);
    if (set_select_message(&tmp, tmp.row_count) != 0) {
        sql_execution_result_clear(&tmp);
        return -1;
    }

    *out = tmp;
    return 0;
}

int executor_execute_select_result(const SelectStmt *stmt, SqlExecutionResult *out) {
    CsvTable *table = NULL;
    int rc = 0;

    if (!stmt || !stmt->table || !out) {
        return -1;
    }

    if (load_select_table(stmt, &table) != 0 || !table) {
        return -1;
    }
    rc = build_projection(table, stmt, out);
    csv_storage_free_table(table);
    return rc;
}

int executor_render_select_tsv(const SqlExecutionResult *result, FILE *out) {
    if (!result || !out || result->statement_type != SQL_STATEMENT_SELECT) {
        return -1;
    }

    for (size_t i = 0; i < result->column_count; i++) {
        if (i > 0 && fputc('\t', out) == EOF) {
            return -1;
        }
        if (fputs(result->columns[i], out) == EOF) {
            return -1;
        }
    }
    if (fputc('\n', out) == EOF) {
        return -1;
    }

    for (size_t r = 0; r < result->row_count; r++) {
        for (size_t c = 0; c < result->column_count; c++) {
            if (c > 0 && fputc('\t', out) == EOF) {
                return -1;
            }
            if (fputs(result->rows[r][c], out) == EOF) {
                return -1;
            }
        }
        if (fputc('\n', out) == EOF) {
            return -1;
        }
    }

    return 0;
}

int executor_execute_select(const SelectStmt *stmt, FILE *out) {
    SqlExecutionResult result = {0};
    int rc = executor_execute_select_result(stmt, &result);
    if (rc != 0) {
        return rc;
    }
    rc = executor_render_select_tsv(&result, out);
    sql_execution_result_clear(&result);
    return rc;
}
