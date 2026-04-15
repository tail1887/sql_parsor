#include "csv_storage.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"
#include "week7/week7_index.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_table_name = "test_data_integrity";
static const char *k_table_path = "data/test_data_integrity.csv";
static const char *k_out_path = "data/test_data_integrity_out.txt";

static int failf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    return 1;
}

static void cleanup_case_files(void) {
    week7_reset();
    remove(k_table_path);
    remove(k_out_path);
}

static int parse_select_sql(const char *sql, SelectStmt **st) {
    Lexer lex;
    lexer_init(&lex, sql, strlen(sql));
    return parser_parse_select(&lex, st);
}

static int run_select_and_capture(const char *sql, char *buf, size_t buf_size) {
    SelectStmt *st = NULL;
    FILE *fp = NULL;
    size_t n = 0;
    int rc = 0;

    if (!buf || buf_size == 0) {
        return -1;
    }
    buf[0] = '\0';

    if (parse_select_sql(sql, &st) != 0 || !st) {
        return -1;
    }

    fp = fopen(k_out_path, "wb+");
    if (!fp) {
        ast_select_stmt_free(st);
        return -1;
    }

    rc = executor_execute_select(st, fp);
    fflush(fp);
    fseek(fp, 0, SEEK_SET);
    n = fread(buf, 1, buf_size - 1, fp);
    buf[n] = '\0';

    fclose(fp);
    ast_select_stmt_free(st);
    return rc;
}

static int write_csv_header(FILE *fp) {
    return fputs("id,name,email\n", fp) < 0 ? -1 : 0;
}

static int write_csv_row(FILE *fp, const char *id, const char *name, const char *email) {
    return fprintf(fp, "%s,%s,%s\n", id, name, email) < 0 ? -1 : 0;
}

static int write_blank_variant(FILE *fp, size_t variant) {
    static const char *variants[] = {
        "\n",
        "\r\n",
        "   \n",
        "\t\n",
        " \t \r\n",
    };
    return fputs(variants[variant % (sizeof(variants) / sizeof(variants[0]))], fp) < 0 ? -1 : 0;
}

static void expected_blank_row(size_t case_no, size_t row_no, char *id_buf, size_t id_size,
                               char *name_buf, size_t name_size, char *email_buf,
                               size_t email_size) {
    long long id = (long long)(case_no * 100 + row_no + 1);
    snprintf(id_buf, id_size, "%lld", id);
    snprintf(name_buf, name_size, "blank_%02u_%02u", (unsigned)case_no, (unsigned)row_no);
    snprintf(email_buf, email_size, "blank_%02u_%02u@example.com", (unsigned)case_no,
             (unsigned)row_no);
}

static int write_duplicate_case(size_t case_no, int64_t *dup_id_out) {
    FILE *fp = fopen(k_table_path, "wb");
    size_t row_count = 4 + (case_no % 4);
    size_t dup_row = 1 + (case_no % (row_count - 1));
    int64_t dup_id = (int64_t)(1000 + case_no);

    if (!fp) {
        return -1;
    }
    if (write_csv_header(fp) != 0) {
        fclose(fp);
        return -1;
    }

    for (size_t row = 0; row < row_count; row++) {
        char id_buf[32];
        char name_buf[64];
        char email_buf[96];
        int64_t id = (row == 0 || row == dup_row) ? dup_id : (int64_t)(5000 + case_no * 10 + row);

        snprintf(id_buf, sizeof id_buf, "%" PRId64, id);
        snprintf(name_buf, sizeof name_buf, "dup_%02u_%02u", (unsigned)case_no, (unsigned)row);
        snprintf(email_buf, sizeof email_buf, "dup_%02u_%02u@example.com", (unsigned)case_no,
                 (unsigned)row);
        if (write_csv_row(fp, id_buf, name_buf, email_buf) != 0) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    *dup_id_out = dup_id;
    return 0;
}

static int write_blank_line_case(size_t case_no, size_t *row_count_out, int64_t *target_id_out,
                                 char *target_name_out, size_t target_name_size) {
    FILE *fp = fopen(k_table_path, "wb");
    size_t row_count = 3 + (case_no % 5);
    size_t target_row = case_no % row_count;

    if (!fp) {
        return -1;
    }
    if (write_csv_header(fp) != 0) {
        fclose(fp);
        return -1;
    }

    if ((case_no % 3) == 0 && write_blank_variant(fp, case_no) != 0) {
        fclose(fp);
        return -1;
    }

    for (size_t row = 0; row < row_count; row++) {
        char id_buf[32];
        char name_buf[64];
        char email_buf[96];

        if (((case_no + row) % 2) == 0 && write_blank_variant(fp, case_no + row) != 0) {
            fclose(fp);
            return -1;
        }

        expected_blank_row(case_no, row, id_buf, sizeof id_buf, name_buf, sizeof name_buf,
                           email_buf, sizeof email_buf);
        if (write_csv_row(fp, id_buf, name_buf, email_buf) != 0) {
            fclose(fp);
            return -1;
        }

        if (((case_no * 3 + row) % 4) == 0 && write_blank_variant(fp, case_no + row + 1) != 0) {
            fclose(fp);
            return -1;
        }

        if (row == target_row) {
            *target_id_out = (int64_t)(case_no * 100 + row + 1);
            snprintf(target_name_out, target_name_size, "%s", name_buf);
        }
    }

    if ((case_no % 5) == 0 && write_blank_variant(fp, case_no + 2) != 0) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *row_count_out = row_count;
    return 0;
}

static void make_invalid_id_text(size_t case_no, char *buf, size_t buf_size) {
    size_t group = case_no / 10;
    switch (case_no % 10) {
    case 0:
        snprintf(buf, buf_size, "bad_%02u", (unsigned)case_no);
        break;
    case 1:
        snprintf(buf, buf_size, "%u_tail", (unsigned)(case_no + 12));
        break;
    case 2:
        buf[0] = '\0';
        break;
    case 3:
        snprintf(buf, buf_size, " %u", (unsigned)(case_no + 3));
        break;
    case 4:
        snprintf(buf, buf_size, "%u ", (unsigned)(case_no + 5));
        break;
    case 5:
        snprintf(buf, buf_size, "%c", group % 2 == 0 ? '+' : '-');
        break;
    case 6:
        snprintf(buf, buf_size, "%u.%u", (unsigned)(case_no + 1), (unsigned)group);
        break;
    case 7:
        snprintf(buf, buf_size, "%ue%u", (unsigned)(case_no + 2), (unsigned)(group + 1));
        break;
    case 8:
        snprintf(buf, buf_size, "%u\t", (unsigned)(case_no + 7));
        break;
    default:
        snprintf(buf, buf_size, "%s", group % 2 == 0 ? "9223372036854775808"
                                                     : "-9223372036854775809");
        break;
    }
}

static int write_invalid_id_case(size_t case_no, char *bad_id_buf, size_t bad_id_size) {
    FILE *fp = fopen(k_table_path, "wb");

    if (!fp) {
        return -1;
    }
    if (write_csv_header(fp) != 0) {
        fclose(fp);
        return -1;
    }

    make_invalid_id_text(case_no, bad_id_buf, bad_id_size);
    if (write_csv_row(fp, bad_id_buf, "broken_row", "broken@example.com") != 0) {
        fclose(fp);
        return -1;
    }
    if (write_csv_row(fp, "200", "valid_a", "valid_a@example.com") != 0) {
        fclose(fp);
        return -1;
    }
    if (write_csv_row(fp, "201", "valid_b", "valid_b@example.com") != 0) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static int test_duplicate_ids(void) {
    for (size_t case_no = 0; case_no < 50; case_no++) {
        char sql[128];
        char out[256];
        int64_t dup_id = 0;

        cleanup_case_files();
        if (write_duplicate_case(case_no, &dup_id) != 0) {
            return failf("duplicate case %u: write failed", (unsigned)case_no);
        }

        if (week7_ensure_loaded(k_table_name) == 0) {
            return failf("duplicate case %u: load unexpectedly succeeded", (unsigned)case_no);
        }
        if (week7_ensure_loaded(k_table_name) == 0) {
            return failf("duplicate case %u: second load unexpectedly succeeded",
                         (unsigned)case_no);
        }

        snprintf(sql, sizeof sql, "SELECT * FROM %s WHERE id = %" PRId64 ";", k_table_name,
                 dup_id);
        if (run_select_and_capture(sql, out, sizeof out) == 0) {
            return failf("duplicate case %u: WHERE id unexpectedly succeeded", (unsigned)case_no);
        }
    }
    return 0;
}

static int test_blank_lines(void) {
    for (size_t case_no = 0; case_no < 50; case_no++) {
        CsvTable *t = NULL;
        size_t row_count = 0;
        int64_t target_id = 0;
        char target_name[64];
        char sql[128];
        char out[256];

        cleanup_case_files();
        if (write_blank_line_case(case_no, &row_count, &target_id, target_name,
                                  sizeof target_name) != 0) {
            return failf("blank case %u: write failed", (unsigned)case_no);
        }

        if (csv_storage_read_table(k_table_name, &t) != 0 || !t) {
            return failf("blank case %u: read_table failed", (unsigned)case_no);
        }
        if (t->row_count != row_count) {
            csv_storage_free_table(t);
            return failf("blank case %u: row_count mismatch got %u want %u", (unsigned)case_no,
                         (unsigned)t->row_count, (unsigned)row_count);
        }

        for (size_t row = 0; row < row_count; row++) {
            char want_id[32];
            char want_name[64];
            char want_email[96];

            expected_blank_row(case_no, row, want_id, sizeof want_id, want_name, sizeof want_name,
                               want_email, sizeof want_email);
            if (strcmp(t->rows[row][0], want_id) != 0 || strcmp(t->rows[row][1], want_name) != 0 ||
                strcmp(t->rows[row][2], want_email) != 0) {
                csv_storage_free_table(t);
                return failf("blank case %u: read_table row %u mismatch", (unsigned)case_no,
                             (unsigned)row);
            }
        }
        csv_storage_free_table(t);

        if (csv_storage_data_row_count(k_table_name, &row_count) != 0) {
            return failf("blank case %u: data_row_count failed", (unsigned)case_no);
        }

        for (size_t row = 0; row < row_count; row++) {
            CsvTable *single = NULL;
            char want_id[32];
            char want_name[64];
            char want_email[96];

            if (csv_storage_read_table_row(k_table_name, row, &single) != 0 || !single) {
                return failf("blank case %u: read_table_row %u failed", (unsigned)case_no,
                             (unsigned)row);
            }
            expected_blank_row(case_no, row, want_id, sizeof want_id, want_name, sizeof want_name,
                               want_email, sizeof want_email);
            if (single->row_count != 1 || strcmp(single->rows[0][0], want_id) != 0 ||
                strcmp(single->rows[0][1], want_name) != 0 ||
                strcmp(single->rows[0][2], want_email) != 0) {
                csv_storage_free_table(single);
                return failf("blank case %u: read_table_row %u mismatch", (unsigned)case_no,
                             (unsigned)row);
            }
            csv_storage_free_table(single);
        }

        snprintf(sql, sizeof sql, "SELECT id, name FROM %s WHERE id = %" PRId64 ";", k_table_name,
                 target_id);
        if (run_select_and_capture(sql, out, sizeof out) != 0) {
            return failf("blank case %u: WHERE id failed", (unsigned)case_no);
        }
        if (strstr(out, "id\tname\n") == NULL || strstr(out, target_name) == NULL) {
            return failf("blank case %u: WHERE id output mismatch", (unsigned)case_no);
        }
    }
    return 0;
}

static int test_invalid_ids(void) {
    for (size_t case_no = 0; case_no < 50; case_no++) {
        char bad_id[64];
        char out[256];

        cleanup_case_files();
        if (write_invalid_id_case(case_no, bad_id, sizeof bad_id) != 0) {
            return failf("invalid case %u: write failed", (unsigned)case_no);
        }

        if (week7_ensure_loaded(k_table_name) == 0) {
            return failf("invalid case %u: load unexpectedly succeeded for '%s'",
                         (unsigned)case_no, bad_id);
        }
        if (week7_ensure_loaded(k_table_name) == 0) {
            return failf("invalid case %u: second load unexpectedly succeeded", (unsigned)case_no);
        }
        if (run_select_and_capture("SELECT * FROM test_data_integrity WHERE id = 200;", out,
                                   sizeof out) == 0) {
            return failf("invalid case %u: WHERE id unexpectedly succeeded", (unsigned)case_no);
        }
    }
    return 0;
}

int main(void) {
    cleanup_case_files();

    if (test_duplicate_ids() != 0) {
        cleanup_case_files();
        return 1;
    }
    if (test_blank_lines() != 0) {
        cleanup_case_files();
        return 1;
    }
    if (test_invalid_ids() != 0) {
        cleanup_case_files();
        return 1;
    }

    cleanup_case_files();
    return 0;
}
