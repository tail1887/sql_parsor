#include "response_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StringBuilder;

static int sb_reserve(StringBuilder *sb, size_t extra) {
    size_t required = sb->len + extra + 1;
    size_t next_cap = sb->cap ? sb->cap : 256;
    char *next = NULL;

    if (required <= sb->cap) {
        return 0;
    }
    while (next_cap < required) {
        next_cap *= 2;
    }
    next = realloc(sb->buf, next_cap);
    if (!next) {
        return -1;
    }
    sb->buf = next;
    sb->cap = next_cap;
    return 0;
}

static int sb_append(StringBuilder *sb, const char *text) {
    size_t len = strlen(text);
    if (sb_reserve(sb, len) != 0) {
        return -1;
    }
    memcpy(sb->buf + sb->len, text, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
    return 0;
}

static int sb_append_char(StringBuilder *sb, char ch) {
    if (sb_reserve(sb, 1) != 0) {
        return -1;
    }
    sb->buf[sb->len++] = ch;
    sb->buf[sb->len] = '\0';
    return 0;
}

static int sb_append_json_string(StringBuilder *sb, const char *text) {
    if (sb_append_char(sb, '"') != 0) {
        return -1;
    }
    for (size_t i = 0; text && text[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)text[i];
        switch (ch) {
        case '"':
            if (sb_append(sb, "\\\"") != 0) {
                return -1;
            }
            break;
        case '\\':
            if (sb_append(sb, "\\\\") != 0) {
                return -1;
            }
            break;
        case '\b':
            if (sb_append(sb, "\\b") != 0) {
                return -1;
            }
            break;
        case '\f':
            if (sb_append(sb, "\\f") != 0) {
                return -1;
            }
            break;
        case '\n':
            if (sb_append(sb, "\\n") != 0) {
                return -1;
            }
            break;
        case '\r':
            if (sb_append(sb, "\\r") != 0) {
                return -1;
            }
            break;
        case '\t':
            if (sb_append(sb, "\\t") != 0) {
                return -1;
            }
            break;
        default:
            if (ch < 0x20) {
                char encoded[7];
                snprintf(encoded, sizeof encoded, "\\u%04x", ch);
                if (sb_append(sb, encoded) != 0) {
                    return -1;
                }
            } else if (sb_append_char(sb, (char)ch) != 0) {
                return -1;
            }
            break;
        }
    }
    return sb_append_char(sb, '"');
}

static const char *http_reason_phrase(int status_code) {
    switch (status_code) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        return "OK";
    }
}

int response_builder_build_result_json(const SqlExecutionResult *result, char **out_body) {
    StringBuilder sb = {0};
    const char *message = NULL;
    int ok = 0;

    if (!result || !out_body) {
        return -1;
    }

    message = result->message ? result->message : "";
    ok = (result->exit_code == 0);

    if (sb_append_char(&sb, '{') != 0) {
        free(sb.buf);
        return -1;
    }
    if (sb_append(&sb, "\"status\":") != 0 ||
        sb_append_json_string(&sb, ok ? "ok" : "error") != 0 ||
        sb_append(&sb, ",\"statementType\":") != 0 ||
        sb_append_json_string(&sb, sql_statement_type_name(result->statement_type)) != 0 ||
        sb_append(&sb, ",\"message\":") != 0 ||
        sb_append_json_string(&sb, message) != 0) {
        free(sb.buf);
        return -1;
    }

    if (ok && result->statement_type == SQL_STATEMENT_INSERT) {
        char number[32];
        snprintf(number, sizeof number, "%zu", result->affected_rows);
        if (sb_append(&sb, ",\"affectedRows\":") != 0 || sb_append(&sb, number) != 0) {
            free(sb.buf);
            return -1;
        }
    }

    if (ok && result->statement_type == SQL_STATEMENT_SELECT) {
        if (sb_append(&sb, ",\"columns\":[") != 0) {
            free(sb.buf);
            return -1;
        }
        for (size_t i = 0; i < result->column_count; i++) {
            if (i > 0 && sb_append_char(&sb, ',') != 0) {
                free(sb.buf);
                return -1;
            }
            if (sb_append_json_string(&sb, result->columns[i]) != 0) {
                free(sb.buf);
                return -1;
            }
        }
        if (sb_append(&sb, "],\"rows\":[") != 0) {
            free(sb.buf);
            return -1;
        }
        for (size_t r = 0; r < result->row_count; r++) {
            if (r > 0 && sb_append_char(&sb, ',') != 0) {
                free(sb.buf);
                return -1;
            }
            if (sb_append_char(&sb, '[') != 0) {
                free(sb.buf);
                return -1;
            }
            for (size_t c = 0; c < result->column_count; c++) {
                if (c > 0 && sb_append_char(&sb, ',') != 0) {
                    free(sb.buf);
                    return -1;
                }
                if (sb_append_json_string(&sb, result->rows[r][c]) != 0) {
                    free(sb.buf);
                    return -1;
                }
            }
            if (sb_append_char(&sb, ']') != 0) {
                free(sb.buf);
                return -1;
            }
        }
        if (sb_append_char(&sb, ']') != 0) {
            free(sb.buf);
            return -1;
        }
    }

    if (!ok) {
        char code[16];
        snprintf(code, sizeof code, "%d", result->exit_code);
        if (sb_append(&sb, ",\"exitCode\":") != 0 || sb_append(&sb, code) != 0) {
            free(sb.buf);
            return -1;
        }
    }

    if (sb_append_char(&sb, '}') != 0) {
        free(sb.buf);
        return -1;
    }

    *out_body = sb.buf;
    return 0;
}

int response_builder_build_error_json(const char *message, char **out_body) {
    StringBuilder sb = {0};

    if (!out_body) {
        return -1;
    }
    if (sb_append(&sb, "{\"status\":\"error\",\"message\":") != 0 ||
        sb_append_json_string(&sb, message ? message : "error") != 0 ||
        sb_append_char(&sb, '}') != 0) {
        free(sb.buf);
        return -1;
    }

    *out_body = sb.buf;
    return 0;
}

int response_builder_build_http_response(int status_code, const char *json_body, char **out_response,
                                         size_t *out_len) {
    StringBuilder sb = {0};
    char header[128];
    size_t body_len = json_body ? strlen(json_body) : 0;

    if (!out_response || !out_len) {
        return -1;
    }

    snprintf(header, sizeof header,
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code, http_reason_phrase(status_code), body_len);

    if (sb_append(&sb, header) != 0 || (json_body && sb_append(&sb, json_body) != 0)) {
        free(sb.buf);
        return -1;
    }

    *out_response = sb.buf;
    *out_len = sb.len;
    return 0;
}
