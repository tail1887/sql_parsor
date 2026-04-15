#include "csv_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CSV_LINE_CAP 8192

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n + 1);
    return p;
}

static int csv_cell_push(char ***arr, size_t *count, size_t *cap, char *v) {
    if (*count >= *cap) {
        size_t ncap = (*cap == 0) ? 4 : (*cap * 2);
        char **nv = realloc(*arr, ncap * sizeof *nv);
        if (!nv) {
            return -1;
        }
        *arr = nv;
        *cap = ncap;
    }
    (*arr)[(*count)++] = v;
    return 0;
}

/* RFC4180 스타일 최소 파서: 콤마 분리, 큰따옴표, "" 이스케이프. */
static int parse_csv_line(const char *line, char ***out_cells, size_t *out_count) {
    char **cells = NULL;
    size_t cnt = 0, cap = 0;
    size_t i = 0;

    for (;;) {
        char *cell = NULL;
        size_t clen = 0, ccap = 16;
        int quoted = 0;

        cell = malloc(ccap);
        if (!cell) {
            return -1;
        }

        if (line[i] == '"') {
            quoted = 1;
            i++;
            while (line[i] != '\0') {
                if (line[i] == '"') {
                    if (line[i + 1] == '"') {
                        if (clen + 1 >= ccap) {
                            ccap *= 2;
                            char *n = realloc(cell, ccap);
                            if (!n) {
                                free(cell);
                                return -1;
                            }
                            cell = n;
                        }
                        cell[clen++] = '"';
                        i += 2;
                    } else {
                        i++;
                        break;
                    }
                } else {
                    if (clen + 1 >= ccap) {
                        ccap *= 2;
                        char *n = realloc(cell, ccap);
                        if (!n) {
                            free(cell);
                            return -1;
                        }
                        cell = n;
                    }
                    cell[clen++] = line[i++];
                }
            }
        } else {
            while (line[i] != '\0' && line[i] != ',' && line[i] != '\n' && line[i] != '\r') {
                if (clen + 1 >= ccap) {
                    ccap *= 2;
                    char *n = realloc(cell, ccap);
                    if (!n) {
                        free(cell);
                        return -1;
                    }
                    cell = n;
                }
                cell[clen++] = line[i++];
            }
        }

        cell[clen] = '\0';

        if (quoted) {
            while (line[i] == ' ') {
                i++;
            }
        }

        if (csv_cell_push(&cells, &cnt, &cap, cell) != 0) {
            free(cell);
            for (size_t k = 0; k < cnt; k++) free(cells[k]);
            free(cells);
            return -1;
        }

        if (line[i] == ',') {
            i++;
            continue;
        }
        break;
    }

    *out_cells = cells;
    *out_count = cnt;
    return 0;
}

static void free_cells(char **cells, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(cells[i]);
    }
    free(cells);
}

static int build_table_path(const char *table, char *buf, size_t n) {
    int w = snprintf(buf, n, "data/%s.csv", table);
    return (w > 0 && (size_t)w < n) ? 0 : -1;
}

int csv_storage_read_table(const char *table, CsvTable **out) {
    *out = NULL;
    char path[512];
    if (build_table_path(table, path, sizeof path) != 0) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    char line[CSV_LINE_CAP];
    if (!fgets(line, sizeof line, fp)) {
        fclose(fp);
        return -1;
    }

    CsvTable *t = calloc(1, sizeof *t);
    if (!t) {
        fclose(fp);
        return -1;
    }

    if (parse_csv_line(line, &t->headers, &t->header_count) != 0) {
        fclose(fp);
        free(t);
        return -1;
    }

    size_t rcap = 4;
    t->rows = calloc(rcap, sizeof *t->rows);
    if (!t->rows) {
        fclose(fp);
        free_cells(t->headers, t->header_count);
        free(t);
        return -1;
    }

    while (fgets(line, sizeof line, fp)) {
        char **cells = NULL;
        size_t ccount = 0;
        if (parse_csv_line(line, &cells, &ccount) != 0) {
            fclose(fp);
            csv_storage_free_table(t);
            return -1;
        }
        if (ccount != t->header_count) {
            free_cells(cells, ccount);
            fclose(fp);
            csv_storage_free_table(t);
            return -1;
        }
        if (t->row_count >= rcap) {
            size_t ncap = rcap * 2;
            char ***nr = realloc(t->rows, ncap * sizeof *nr);
            if (!nr) {
                free_cells(cells, ccount);
                fclose(fp);
                csv_storage_free_table(t);
                return -1;
            }
            t->rows = nr;
            rcap = ncap;
        }
        t->rows[t->row_count++] = cells;
    }

    fclose(fp);
    *out = t;
    return 0;
}

int csv_storage_read_table_row(const char *table, size_t row_index, CsvTable **out) {
    if (!table || !out) {
        return -1;
    }
    *out = NULL;
    char path[512];
    if (build_table_path(table, path, sizeof path) != 0) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    char line[CSV_LINE_CAP];
    if (!fgets(line, sizeof line, fp)) {
        fclose(fp);
        return -1;
    }

    CsvTable *t = calloc(1, sizeof *t);
    if (!t) {
        fclose(fp);
        return -1;
    }
    if (parse_csv_line(line, &t->headers, &t->header_count) != 0) {
        fclose(fp);
        free(t);
        return -1;
    }
    t->rows = calloc(1, sizeof *t->rows);
    if (!t->rows) {
        fclose(fp);
        free_cells(t->headers, t->header_count);
        free(t);
        return -1;
    }

    size_t cur = 0;
    int found = 0;
    while (fgets(line, sizeof line, fp)) {
        size_t i = 0;
        while (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n') {
            i++;
        }
        if (line[i] == '\0') {
            continue;
        }

        char **cells = NULL;
        size_t ccount = 0;
        if (parse_csv_line(line, &cells, &ccount) != 0) {
            fclose(fp);
            csv_storage_free_table(t);
            return -1;
        }
        if (ccount != t->header_count) {
            free_cells(cells, ccount);
            fclose(fp);
            csv_storage_free_table(t);
            return -1;
        }

        if (cur == row_index) {
            t->rows[0] = cells;
            t->row_count = 1;
            found = 1;
            break;
        }
        cur++;
        free_cells(cells, ccount);
    }

    fclose(fp);
    if (!found) {
        t->row_count = 0;
    }
    *out = t;
    return 0;
}

static int append_csv_escaped(FILE *fp, const char *s) {
    if (fputc('"', fp) == EOF) return -1;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '"') {
            if (fputc('"', fp) == EOF) return -1;
            if (fputc('"', fp) == EOF) return -1;
        } else {
            if (fputc((unsigned char)s[i], fp) == EOF) return -1;
        }
    }
    if (fputc('"', fp) == EOF) return -1;
    return 0;
}

int csv_storage_append_insert_row(const char *table, const SqlValue *values, size_t value_count) {
    char path[512];
    if (build_table_path(table, path, sizeof path) != 0) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    char line[CSV_LINE_CAP];
    if (!fgets(line, sizeof line, fp)) {
        fclose(fp);
        return -1;
    }

    char **headers = NULL;
    size_t header_count = 0;
    if (parse_csv_line(line, &headers, &header_count) != 0) {
        fclose(fp);
        return -1;
    }
    free_cells(headers, header_count);
    fclose(fp);

    if (header_count != value_count) {
        return -1;
    }

    int need_newline = 0;
    fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return -1;
    }
    if (sz > 0) {
        if (fseek(fp, -1, SEEK_END) != 0) {
            fclose(fp);
            return -1;
        }
        int last = fgetc(fp);
        if (last != '\n' && last != '\r') {
            need_newline = 1;
        }
    }
    fclose(fp);

    fp = fopen(path, "ab");
    if (!fp) {
        return -1;
    }

    if (need_newline) {
        if (fputc('\n', fp) == EOF) {
            fclose(fp);
            return -1;
        }
    }

    for (size_t i = 0; i < value_count; i++) {
        if (i > 0 && fputc(',', fp) == EOF) {
            fclose(fp);
            return -1;
        }

        switch (values[i].kind) {
        case SQL_VALUE_INT:
            if (fputs(values[i].text ? values[i].text : "0", fp) == EOF) {
                fclose(fp);
                return -1;
            }
            break;
        case SQL_VALUE_STRING:
            if (append_csv_escaped(fp, values[i].text ? values[i].text : "") != 0) {
                fclose(fp);
                return -1;
            }
            break;
        case SQL_VALUE_NULL:
            /* NULL 은 빈 필드로 저장한다. */
            break;
        default:
            fclose(fp);
            return -1;
        }
    }

    if (fclose(fp) != 0) {
        return -1;
    }
    return 0;
}

int csv_storage_column_count(const char *table, size_t *out_count) {
    if (!table || !out_count) {
        return -1;
    }
    *out_count = 0;
    char path[512];
    if (build_table_path(table, path, sizeof path) != 0) {
        return -1;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    char line[CSV_LINE_CAP];
    if (!fgets(line, sizeof line, fp)) {
        fclose(fp);
        return -1;
    }
    char **cells = NULL;
    size_t n = 0;
    if (parse_csv_line(line, &cells, &n) != 0) {
        fclose(fp);
        return -1;
    }
    free_cells(cells, n);
    fclose(fp);
    *out_count = n;
    return 0;
}

// csv_storage_data_row_count 함수 정의 이 함수는 테이블의 데이터 행 개수를 구하는 함수이다. 
int csv_storage_data_row_count(const char *table, size_t *out_count) {
    if (!table || !out_count) {
        return -1;
    }
    *out_count = 0;
    char path[512];
    if (build_table_path(table, path, sizeof path) != 0) {
        return -1;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    char line[CSV_LINE_CAP];
    if (!fgets(line, sizeof line, fp)) {
        fclose(fp);
        return -1;
    }
    size_t rows = 0;
    while (fgets(line, sizeof line, fp)) {
        size_t i = 0;
        while (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n') {
            i++;
        }
        if (line[i] != '\0') {
            rows++;
        }
    }
    fclose(fp);
    *out_count = rows;
    return 0;
}

void csv_storage_free_table(CsvTable *table) {
    if (!table) {
        return;
    }
    free_cells(table->headers, table->header_count);
    for (size_t r = 0; r < table->row_count; r++) {
        free_cells(table->rows[r], table->header_count);
    }
    free(table->rows);
    free(table);
}
