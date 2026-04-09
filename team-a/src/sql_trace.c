#include "sql_processor.h"

#include "ast.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"

#include <ctype.h>
#include <stdio.h>
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

static void json_write_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':
            fputs("\\\"", f);
            break;
        case '\\':
            fputs("\\\\", f);
            break;
        case '\n':
            fputs("\\n", f);
            break;
        case '\r':
            fputs("\\r", f);
            break;
        case '\t':
            fputs("\\t", f);
            break;
        default:
            if (c < 0x20) {
                fprintf(f, "\\u%04x", c);
            } else {
                fputc((int)c, f);
            }
            break;
        }
    }
    fputc('"', f);
}

static const char *token_kind_name(TokenKind kind) {
    switch (kind) {
    case TOKEN_EOF:
        return "TOKEN_EOF";
    case TOKEN_ERROR:
        return "TOKEN_ERROR";
    case TOKEN_INSERT:
        return "TOKEN_INSERT";
    case TOKEN_INTO:
        return "TOKEN_INTO";
    case TOKEN_VALUES:
        return "TOKEN_VALUES";
    case TOKEN_SELECT:
        return "TOKEN_SELECT";
    case TOKEN_FROM:
        return "TOKEN_FROM";
    case TOKEN_NULL:
        return "TOKEN_NULL";
    case TOKEN_IDENTIFIER:
        return "TOKEN_IDENTIFIER";
    case TOKEN_INTEGER:
        return "TOKEN_INTEGER";
    case TOKEN_STRING:
        return "TOKEN_STRING";
    case TOKEN_LPAREN:
        return "TOKEN_LPAREN";
    case TOKEN_RPAREN:
        return "TOKEN_RPAREN";
    case TOKEN_COMMA:
        return "TOKEN_COMMA";
    case TOKEN_SEMICOLON:
        return "TOKEN_SEMICOLON";
    case TOKEN_STAR:
        return "TOKEN_STAR";
    case TOKEN_PLUS:
        return "TOKEN_PLUS";
    case TOKEN_MINUS:
        return "TOKEN_MINUS";
    default:
        return "TOKEN_UNKNOWN";
    }
}

static void trace_lexer_tokens(FILE *trace, const char *stmt, size_t len, size_t stmt_no) {
    Lexer lex;
    Token tok;
    int first = 1;
    lexer_init(&lex, stmt, len);
    fprintf(trace, "{\"step\":\"lexer_tokens\",\"statementNo\":%zu,\"tokens\":[", stmt_no);
    while (lexer_next(&lex, &tok) == 0) {
        char *text = dup_slice(tok.text ? tok.text : "", tok.text_len);
        if (!text) {
            break;
        }
        if (!first) {
            fputc(',', trace);
        }
        first = 0;
        fputc('{', trace);
        fputs("\"kind\":", trace);
        json_write_string(trace, token_kind_name(tok.kind));
        fputs(",\"text\":", trace);
        json_write_string(trace, text);
        fprintf(trace, ",\"line\":%u,\"column\":%u}", tok.line, tok.column);
        free(text);
        if (tok.kind == TOKEN_EOF || tok.kind == TOKEN_ERROR) {
            break;
        }
    }
    fputs("]}\n", trace);
}

static void trace_value(FILE *trace, const SqlValue *v) {
    fputc('{', trace);
    if (v->kind == SQL_VALUE_INT) {
        fputs("\"kind\":\"SQL_VALUE_INT\",\"text\":", trace);
        json_write_string(trace, v->text ? v->text : "");
    } else if (v->kind == SQL_VALUE_STRING) {
        fputs("\"kind\":\"SQL_VALUE_STRING\",\"text\":", trace);
        json_write_string(trace, v->text ? v->text : "");
    } else {
        fputs("\"kind\":\"SQL_VALUE_NULL\",\"text\":null", trace);
    }
    fputc('}', trace);
}

static void trace_insert_ast(FILE *trace, const InsertStmt *ins) {
    fputs("{\"type\":\"InsertStmt\",\"table\":", trace);
    json_write_string(trace, ins->table ? ins->table : "");
    fputs(",\"values\":[", trace);
    for (size_t i = 0; i < ins->value_count; i++) {
        if (i > 0) {
            fputc(',', trace);
        }
        trace_value(trace, &ins->values[i]);
    }
    fputs("]}", trace);
}

static void trace_select_ast(FILE *trace, const SelectStmt *sel) {
    fputs("{\"type\":\"SelectStmt\",\"table\":", trace);
    json_write_string(trace, sel->table ? sel->table : "");
    fprintf(trace, ",\"selectAll\":%s,\"columns\":[", sel->select_all ? "true" : "false");
    for (size_t i = 0; i < sel->column_count; i++) {
        if (i > 0) {
            fputc(',', trace);
        }
        json_write_string(trace, sel->columns[i] ? sel->columns[i] : "");
    }
    fputs("]}", trace);
}

static int execute_one_statement_trace(const char *stmt, size_t len, FILE *out, FILE *err, FILE *trace,
                                       size_t stmt_no) {
    const char *s = stmt;
    const char *e = stmt + len;
    s = trim_start(s, e);
    e = trim_end(s, e);
    if (s >= e) {
        return 0;
    }

    fprintf(trace, "{\"step\":\"statement_start\",\"statementNo\":%zu,\"sql\":", stmt_no);
    {
        char *st = dup_slice(s, (size_t)(e - s));
        if (!st) {
            return 3;
        }
        json_write_string(trace, st);
        free(st);
    }
    fputs("}\n", trace);
    trace_lexer_tokens(trace, s, (size_t)(e - s), stmt_no);

    Lexer lex;
    lexer_init(&lex, s, (size_t)(e - s));

    InsertStmt *ins = NULL;
    if (parser_parse_insert(&lex, &ins) == 0 && ins != NULL) {
        fprintf(trace, "{\"step\":\"parser_result\",\"statementNo\":%zu,\"parser\":\"insert\",\"ok\":true,\"ast\":",
                stmt_no);
        trace_insert_ast(trace, ins);
        fputs("}\n", trace);
        fprintf(trace, "{\"step\":\"executor_call\",\"statementNo\":%zu,\"executor\":\"executor_execute_insert\"}\n",
                stmt_no);
        int rc = executor_execute_insert(ins);
        ast_insert_stmt_free(ins);
        if (rc != 0) {
            fprintf(err, "exec error: statement %zu failed (INSERT)\n", stmt_no);
            fprintf(trace, "{\"step\":\"statement_end\",\"statementNo\":%zu,\"status\":\"exec_error\",\"code\":3}\n",
                    stmt_no);
            return 3;
        }
        fprintf(trace, "{\"step\":\"statement_end\",\"statementNo\":%zu,\"status\":\"ok\",\"code\":0}\n", stmt_no);
        return 0;
    }

    SelectStmt *sel = NULL;
    lexer_init(&lex, s, (size_t)(e - s));
    if (parser_parse_select(&lex, &sel) == 0 && sel != NULL) {
        fprintf(trace, "{\"step\":\"parser_result\",\"statementNo\":%zu,\"parser\":\"select\",\"ok\":true,\"ast\":",
                stmt_no);
        trace_select_ast(trace, sel);
        fputs("}\n", trace);
        fprintf(trace, "{\"step\":\"executor_call\",\"statementNo\":%zu,\"executor\":\"executor_execute_select\"}\n",
                stmt_no);
        int rc = executor_execute_select(sel, out);
        ast_select_stmt_free(sel);
        if (rc != 0) {
            fprintf(err, "exec error: statement %zu failed (SELECT)\n", stmt_no);
            fprintf(trace, "{\"step\":\"statement_end\",\"statementNo\":%zu,\"status\":\"exec_error\",\"code\":3}\n",
                    stmt_no);
            return 3;
        }
        fprintf(trace, "{\"step\":\"statement_end\",\"statementNo\":%zu,\"status\":\"ok\",\"code\":0}\n", stmt_no);
        return 0;
    }

    fprintf(trace, "{\"step\":\"parser_result\",\"statementNo\":%zu,\"parser\":\"insert/select\",\"ok\":false}\n",
            stmt_no);
    fprintf(err, "parse error: statement %zu\n", stmt_no);
    fprintf(trace, "{\"step\":\"statement_end\",\"statementNo\":%zu,\"status\":\"parse_error\",\"code\":2}\n", stmt_no);
    return 2;
}

static int run_sql_text_trace(const char *sql, size_t len, FILE *out, FILE *err, FILE *trace) {
    size_t start = 0;
    int in_string = 0;
    size_t stmt_no = 0;

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
            {
                int rc = execute_one_statement_trace(sql + start, i - start + 1, out, err, trace, stmt_no);
                if (rc != 0) {
                    return rc;
                }
            }
            start = i + 1;
        }
    }

    if (start < len) {
        stmt_no++;
        {
            int rc = execute_one_statement_trace(sql + start, len - start, out, err, trace, stmt_no);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}

int sql_processor_run_file_trace(const char *path, FILE *out, FILE *err, FILE *trace) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(err, "io error: failed to open %s\n", path);
        if (trace) {
            fprintf(trace, "{\"step\":\"process_end\",\"exitCode\":3}\n");
        }
        return 3;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        fprintf(err, "io error: failed to seek %s\n", path);
        if (trace) {
            fprintf(trace, "{\"step\":\"process_end\",\"exitCode\":3}\n");
        }
        return 3;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        fprintf(err, "io error: failed to size %s\n", path);
        if (trace) {
            fprintf(trace, "{\"step\":\"process_end\",\"exitCode\":3}\n");
        }
        return 3;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        fprintf(err, "io error: failed to rewind %s\n", path);
        if (trace) {
            fprintf(trace, "{\"step\":\"process_end\",\"exitCode\":3}\n");
        }
        return 3;
    }

    char *buf = calloc((size_t)sz + 1, 1);
    if (!buf) {
        fclose(fp);
        fprintf(err, "io error: out of memory\n");
        if (trace) {
            fprintf(trace, "{\"step\":\"process_end\",\"exitCode\":3}\n");
        }
        return 3;
    }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (n != (size_t)sz) {
        free(buf);
        fprintf(err, "io error: failed to read %s\n", path);
        if (trace) {
            fprintf(trace, "{\"step\":\"process_end\",\"exitCode\":3}\n");
        }
        return 3;
    }

    int rc = run_sql_text_trace(buf, n, out, err, trace);
    free(buf);
    if (trace) {
        fprintf(trace, "{\"step\":\"process_end\",\"exitCode\":%d}\n", rc);
    }
    return rc;
}
