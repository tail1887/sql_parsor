#include "sql_processor.h"

#include "ast.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *dup_slice(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) {
        return NULL;
    }
    if (n > 0) {
        memcpy(p, s, n);
    }
    p[n] = '\0';
    return p;
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

static int execute_one_statement(const char *stmt, size_t len, FILE *out, FILE *err, size_t stmt_no) {
    const char *s = stmt;
    const char *e = stmt + len;
    s = trim_start(s, e);
    e = trim_end(s, e);
    if (s >= e) {
        return 0;
    }

    Lexer lex;
    lexer_init(&lex, s, (size_t)(e - s));

    InsertStmt *ins = NULL;
    if (parser_parse_insert(&lex, &ins) == 0 && ins != NULL) {
        int rc = executor_execute_insert(ins);
        ast_insert_stmt_free(ins);
        if (rc != 0) {
            fprintf(err, "exec error: statement %zu failed (INSERT)\n", stmt_no);
            return 3;
        }
        return 0;
    }

    SelectStmt *sel = NULL;
    lexer_init(&lex, s, (size_t)(e - s));
    if (parser_parse_select(&lex, &sel) == 0 && sel != NULL) {
        int rc = executor_execute_select(sel, out);
        ast_select_stmt_free(sel);
        if (rc != 0) {
            fprintf(err, "exec error: statement %zu failed (SELECT)\n", stmt_no);
            return 3;
        }
        return 0;
    }

    fprintf(err, "parse error: statement %zu\n", stmt_no);
    return 2;
}

static int run_sql_text(const char *sql, size_t len, FILE *out, FILE *err) {
    size_t start = 0;
    int in_string = 0;
    size_t stmt_no = 0;

    /* UTF-8 BOM(EF BB BF) 이 있으면 첫 문장 파싱 전에 건너뛴다. */
    if (len >= 3 && (unsigned char)sql[0] == 0xEF && (unsigned char)sql[1] == 0xBB &&
        (unsigned char)sql[2] == 0xBF) {
        start = 3;
    }

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
            int rc = execute_one_statement(sql + start, i - start + 1, out, err, stmt_no);
            if (rc != 0) {
                return rc;
            }
            start = i + 1;
        }
    }

    if (start < len) {
        stmt_no++;
        int rc = execute_one_statement(sql + start, len - start, out, err, stmt_no);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

int sql_processor_run_file(const char *path, FILE *out, FILE *err) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(err, "io error: failed to open %s\n", path);
        return 3;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        fprintf(err, "io error: failed to seek %s\n", path);
        return 3;
    }
    long sz = ftell(fp);
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

    char *buf = calloc((size_t)sz + 1, 1);
    if (!buf) {
        fclose(fp);
        fprintf(err, "io error: out of memory\n");
        return 3;
    }

    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (n != (size_t)sz) {
        free(buf);
        fprintf(err, "io error: failed to read %s\n", path);
        return 3;
    }

    int rc = run_sql_text(buf, n, out, err);
    free(buf);
    return rc;
}
