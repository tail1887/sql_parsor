#include "sql_processor.h"

#include "ast.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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

static const char *trim_start(const char *s, const char *end) {
    while (s < end && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static const char *trim_end(const char *s, const char *end) {
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    return end;
}

static size_t skip_utf8_bom(const char *sql, size_t len) {
    if (len >= 3 && (unsigned char)sql[0] == 0xEF && (unsigned char)sql[1] == 0xBB &&
        (unsigned char)sql[2] == 0xBF) {
        return 3;
    }
    return 0;
}

static int set_result_message(SqlExecutionResult *result, const char *message) {
    char *copy = dup_cstr(message);
    if (!copy) {
        return -1;
    }
    result->message = copy;
    return 0;
}

static void write_error_line(FILE *err, const char *message) {
    if (!err || !message) {
        return;
    }
    fprintf(err, "%s\n", message);
}

static void move_or_clear_result(SqlExecutionResult *dst, SqlExecutionResult *src) {
    if (dst) {
        *dst = *src;
        memset(src, 0, sizeof *src);
        return;
    }
    sql_execution_result_clear(src);
}

static int finalize_error(SqlExecutionResult *result, FILE *err, SqlStatementType type, int exit_code,
                          const char *message) {
    SqlExecutionResult tmp = {0};

    tmp.statement_type = type;
    tmp.exit_code = exit_code;
    if (set_result_message(&tmp, message) != 0) {
        tmp.exit_code = exit_code;
    }
    write_error_line(err, message);
    move_or_clear_result(result, &tmp);
    return exit_code;
}

static int finalize_insert_success(SqlExecutionResult *result) {
    SqlExecutionResult tmp = {0};

    tmp.statement_type = SQL_STATEMENT_INSERT;
    tmp.exit_code = 0;
    tmp.affected_rows = 1;
    if (set_result_message(&tmp, "1 row inserted") != 0) {
        sql_execution_result_clear(&tmp);
        return 3;
    }
    move_or_clear_result(result, &tmp);
    return 0;
}

static int execute_one_statement(const char *stmt, size_t len, SqlExecutionResult *result, FILE *out, FILE *err,
                                 size_t stmt_no) {
    const char *s = stmt;
    const char *e = stmt + len;
    Lexer lex;
    SqlExecutionResult tmp = {0};
    char message[128];
    InsertStmt *ins = NULL;
    SelectStmt *sel = NULL;

    s = trim_start(s, e);
    e = trim_end(s, e);
    if (s >= e) {
        return 0;
    }

    lexer_init(&lex, s, (size_t)(e - s));
    if (parser_parse_insert(&lex, &ins) == 0 && ins != NULL) {
        int rc = executor_execute_insert(ins);
        ast_insert_stmt_free(ins);
        if (rc != 0) {
            snprintf(message, sizeof message, "exec error: statement %zu failed (INSERT)", stmt_no);
            return finalize_error(result, err, SQL_STATEMENT_INSERT, 3, message);
        }
        return finalize_insert_success(result);
    }

    lexer_init(&lex, s, (size_t)(e - s));
    if (parser_parse_select(&lex, &sel) == 0 && sel != NULL) {
        int rc = executor_execute_select_result(sel, &tmp);
        ast_select_stmt_free(sel);
        if (rc != 0) {
            snprintf(message, sizeof message, "exec error: statement %zu failed (SELECT)", stmt_no);
            return finalize_error(result, err, SQL_STATEMENT_SELECT, 3, message);
        }
        if (out && executor_render_select_tsv(&tmp, out) != 0) {
            sql_execution_result_clear(&tmp);
            snprintf(message, sizeof message, "exec error: statement %zu failed (SELECT)", stmt_no);
            return finalize_error(result, err, SQL_STATEMENT_SELECT, 3, message);
        }
        move_or_clear_result(result, &tmp);
        return 0;
    }

    snprintf(message, sizeof message, "parse error: statement %zu", stmt_no);
    return finalize_error(result, err, SQL_STATEMENT_UNKNOWN, 2, message);
}

static int count_nonempty_statements(const char *sql, size_t len, const char **first_stmt, size_t *first_len) {
    size_t start = skip_utf8_bom(sql, len);
    size_t count = 0;
    int in_string = 0;

    for (size_t i = 0; i < len; i++) {
        char c = sql[i];

        if (in_string) {
            if (c == '\'' && i + 1 < len && sql[i + 1] == '\'') {
                i++;
                continue;
            }
            if (c == '\'') {
                in_string = 0;
            }
            continue;
        }

        if (c == '\'') {
            in_string = 1;
            continue;
        }

        if (c == ';') {
            const char *stmt_start = sql + start;
            const char *stmt_end = sql + i + 1;
            const char *trimmed_start = trim_start(stmt_start, stmt_end);
            const char *trimmed_end = trim_end(trimmed_start, stmt_end);

            if (trimmed_start < trimmed_end) {
                count++;
                if (count == 1 && first_stmt && first_len) {
                    *first_stmt = stmt_start;
                    *first_len = (size_t)(stmt_end - stmt_start);
                }
            }
            start = i + 1;
        }
    }

    if (start < len) {
        const char *stmt_start = sql + start;
        const char *stmt_end = sql + len;
        const char *trimmed_start = trim_start(stmt_start, stmt_end);
        const char *trimmed_end = trim_end(trimmed_start, stmt_end);

        if (trimmed_start < trimmed_end) {
            count++;
            if (count == 1 && first_stmt && first_len) {
                *first_stmt = stmt_start;
                *first_len = (size_t)(stmt_end - stmt_start);
            }
        }
    }

    return (int)count;
}

static int run_sql_script(const char *sql, size_t len, FILE *out, FILE *err) {
    size_t start = skip_utf8_bom(sql, len);
    int in_string = 0;
    size_t stmt_no = 0;

    for (size_t i = 0; i < len; i++) {
        char c = sql[i];

        if (in_string) {
            if (c == '\'' && i + 1 < len && sql[i + 1] == '\'') {
                i++;
                continue;
            }
            if (c == '\'') {
                in_string = 0;
            }
            continue;
        }

        if (c == '\'') {
            in_string = 1;
            continue;
        }

        if (c == ';') {
            stmt_no++;
            int rc = execute_one_statement(sql + start, i - start + 1, NULL, out, err, stmt_no);
            if (rc != 0) {
                return rc;
            }
            start = i + 1;
        }
    }

    if (start < len) {
        stmt_no++;
        return execute_one_statement(sql + start, len - start, NULL, out, err, stmt_no);
    }

    return 0;
}

int sql_processor_run_file(const char *path, FILE *out, FILE *err) {
    FILE *fp = fopen(path, "rb");
    char *buf = NULL;
    long sz = 0;
    size_t n = 0;
    int rc = 0;

    if (!fp) {
        fprintf(err, "io error: failed to open %s\n", path);
        return 3;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        fprintf(err, "io error: failed to seek %s\n", path);
        return 3;
    }
    sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        fprintf(err, "io error: failed to size %s\n", path);
        return 3;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        fprintf(err, "io error: failed to rewind %s\n", path);
        return 3;
    }

    buf = calloc((size_t)sz + 1, 1);
    if (!buf) {
        fclose(fp);
        fprintf(err, "io error: out of memory\n");
        return 3;
    }

    n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (n != (size_t)sz) {
        free(buf);
        fprintf(err, "io error: failed to read %s\n", path);
        return 3;
    }

    rc = run_sql_script(buf, n, out, err);
    free(buf);
    return rc;
}

int sql_processor_run_text(const char *sql, SqlExecutionResult *out, FILE *err) {
    const char *stmt = NULL;
    size_t stmt_len = 0;
    int count = 0;

    if (!sql || !out) {
        return 3;
    }

    memset(out, 0, sizeof *out);
    count = count_nonempty_statements(sql, strlen(sql), &stmt, &stmt_len);
    if (count != 1 || !stmt) {
        return finalize_error(out, err, SQL_STATEMENT_UNKNOWN, 2,
                              "parse error: exactly one statement required");
    }

    return execute_one_statement(stmt, stmt_len, out, NULL, err, 1);
}

void sql_processor_free_result(SqlExecutionResult *out) { sql_execution_result_clear(out); }
