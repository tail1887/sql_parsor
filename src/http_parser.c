#include "http_parser.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define HTTP_MAX_REQUEST_SIZE 65536

static char *dup_slice(const char *start, size_t len) {
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    if (len > 0) {
        memcpy(copy, start, len);
    }
    copy[len] = '\0';
    return copy;
}

static char *dup_cstr(const char *text) {
    size_t len = strlen(text);
    return dup_slice(text, len);
}

static void set_error(int *out_status, char **out_error, int status, const char *message) {
    if (out_status) {
        *out_status = status;
    }
    if (out_error) {
        *out_error = dup_cstr(message);
    }
}

static int ascii_strncasecmp_local(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (tolower(ca) != tolower(cb)) {
            return tolower(ca) - tolower(cb);
        }
    }
    return 0;
}

static const char *find_header_end(const char *buf, size_t len) {
    if (len < 4) {
        return NULL;
    }
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return buf + i;
        }
    }
    return NULL;
}

static int parse_content_length_header(const char *raw, size_t header_len, size_t *out_length) {
    const char *cursor = raw;
    const char *end = raw + header_len;

    while (cursor < end) {
        const char *line_end = strstr(cursor, "\r\n");
        size_t line_len = 0;
        if (!line_end || line_end > end) {
            break;
        }
        line_len = (size_t)(line_end - cursor);
        if (line_len >= 15 && ascii_strncasecmp_local(cursor, "Content-Length:", 15) == 0) {
            const char *value = cursor + 15;
            unsigned long parsed = 0;
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            if (*value == '\0') {
                return -1;
            }
            for (; value < line_end; value++) {
                if (!isdigit((unsigned char)*value)) {
                    return -1;
                }
                parsed = parsed * 10UL + (unsigned long)(*value - '0');
                if (parsed > HTTP_MAX_REQUEST_SIZE) {
                    return -1;
                }
            }
            *out_length = (size_t)parsed;
            return 1;
        }
        cursor = line_end + 2;
    }
    return 0;
}

static int json_skip_ws(const char *body, size_t len, size_t *index) {
    while (*index < len && isspace((unsigned char)body[*index])) {
        (*index)++;
    }
    return 0;
}

static int json_parse_string(const char *body, size_t len, size_t *index, char **out_value) {
    char *buffer = NULL;
    size_t cap = 16;
    size_t used = 0;

    if (*index >= len || body[*index] != '"') {
        return -1;
    }
    (*index)++;

    buffer = malloc(cap);
    if (!buffer) {
        return -1;
    }

    while (*index < len) {
        char ch = body[*index];
        if (ch == '"') {
            (*index)++;
            buffer[used] = '\0';
            *out_value = buffer;
            return 0;
        }
        if (ch == '\\') {
            (*index)++;
            if (*index >= len) {
                free(buffer);
                return -1;
            }
            switch (body[*index]) {
            case '"':
            case '\\':
            case '/':
                ch = body[*index];
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            default:
                free(buffer);
                return -1;
            }
        }
        if (used + 2 > cap) {
            size_t next_cap = cap * 2;
            char *next = realloc(buffer, next_cap);
            if (!next) {
                free(buffer);
                return -1;
            }
            buffer = next;
            cap = next_cap;
        }
        buffer[used++] = ch;
        (*index)++;
    }

    free(buffer);
    return -1;
}

static int extract_sql_field(const char *body, size_t len, char **out_sql) {
    size_t index = 0;
    char *key = NULL;
    char *value = NULL;
    int found = 0;

    json_skip_ws(body, len, &index);
    if (index >= len || body[index] != '{') {
        return -1;
    }
    index++;

    for (;;) {
        json_skip_ws(body, len, &index);
        if (index >= len) {
            return -1;
        }
        if (body[index] == '}') {
            index++;
            break;
        }
        if (json_parse_string(body, len, &index, &key) != 0) {
            return -1;
        }
        json_skip_ws(body, len, &index);
        if (index >= len || body[index] != ':') {
            free(key);
            return -1;
        }
        index++;
        json_skip_ws(body, len, &index);
        if (json_parse_string(body, len, &index, &value) != 0) {
            free(key);
            return -1;
        }
        if (strcmp(key, "sql") == 0) {
            free(*out_sql);
            *out_sql = value;
            value = NULL;
            found = 1;
        }
        free(key);
        free(value);
        key = NULL;
        value = NULL;

        json_skip_ws(body, len, &index);
        if (index >= len) {
            return -1;
        }
        if (body[index] == ',') {
            index++;
            continue;
        }
        if (body[index] == '}') {
            index++;
            break;
        }
        return -1;
    }

    json_skip_ws(body, len, &index);
    if (index != len || !found) {
        return -1;
    }
    return 0;
}

int http_parser_read_request(int fd, char **out_raw, size_t *out_len, int *out_status, char **out_error) {
    char *buffer = NULL;
    size_t used = 0;
    size_t cap = 2048;
    size_t content_length = 0;
    int have_content_length = 0;
    const char *header_end = NULL;

    if (!out_raw || !out_len) {
        set_error(out_status, out_error, 500, "internal server error");
        return -1;
    }

    *out_raw = NULL;
    *out_len = 0;
    if (out_status) {
        *out_status = 0;
    }
    if (out_error) {
        *out_error = NULL;
    }

    buffer = calloc(cap, 1);
    if (!buffer) {
        set_error(out_status, out_error, 500, "internal server error");
        return -1;
    }

    for (;;) {
        ssize_t n = recv(fd, buffer + used, cap - used - 1, 0);
        if (n < 0) {
            free(buffer);
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                set_error(out_status, out_error, 400, "request read timed out");
            } else {
                set_error(out_status, out_error, 400, "failed to read request");
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
        buffer[used] = '\0';

        header_end = find_header_end(buffer, used);
        if (header_end && !have_content_length) {
            int rc = parse_content_length_header(buffer, (size_t)(header_end - buffer), &content_length);
            if (rc < 0) {
                free(buffer);
                set_error(out_status, out_error, 400, "invalid Content-Length header");
                return -1;
            }
            have_content_length = rc > 0;
        }

        if (header_end) {
            size_t total_needed = (size_t)(header_end - buffer) + 4 + (have_content_length ? content_length : 0);
            if (used >= total_needed) {
                *out_raw = buffer;
                *out_len = used;
                return 0;
            }
        }

        if (used + 1 >= cap) {
            size_t next_cap = cap * 2;
            char *next = NULL;
            if (next_cap > HTTP_MAX_REQUEST_SIZE) {
                free(buffer);
                set_error(out_status, out_error, 400, "request too large");
                return -1;
            }
            next = realloc(buffer, next_cap);
            if (!next) {
                free(buffer);
                set_error(out_status, out_error, 500, "internal server error");
                return -1;
            }
            memset(next + cap, 0, next_cap - cap);
            buffer = next;
            cap = next_cap;
        }
    }

    if (!header_end) {
        free(buffer);
        set_error(out_status, out_error, 400, "incomplete HTTP request");
        return -1;
    }

    *out_raw = buffer;
    *out_len = used;
    return 0;
}

int http_parser_parse_request(const char *raw, size_t raw_len, HttpRequest *out, int *out_status,
                              char **out_error) {
    HttpRequest tmp = {0};
    const char *header_end = NULL;
    const char *line_end = NULL;
    const char *cursor = NULL;
    size_t header_len = 0;
    size_t content_length = 0;
    int content_header_state = 0;

    if (!raw || !out) {
        set_error(out_status, out_error, 500, "internal server error");
        return -1;
    }

    if (out_status) {
        *out_status = 0;
    }
    if (out_error) {
        *out_error = NULL;
    }

    header_end = find_header_end(raw, raw_len);
    if (!header_end) {
        set_error(out_status, out_error, 400, "malformed HTTP request");
        return -1;
    }

    line_end = strstr(raw, "\r\n");
    if (!line_end || line_end > header_end) {
        set_error(out_status, out_error, 400, "malformed request line");
        return -1;
    }

    {
        const char *sp1 = memchr(raw, ' ', (size_t)(line_end - raw));
        const char *sp2 = sp1 ? memchr(sp1 + 1, ' ', (size_t)(line_end - sp1 - 1)) : NULL;
        if (!sp1 || !sp2 || sp1 == raw || sp2 == sp1 + 1) {
            set_error(out_status, out_error, 400, "malformed request line");
            return -1;
        }
        if (strncmp(sp2 + 1, "HTTP/1.", 7) != 0) {
            set_error(out_status, out_error, 400, "unsupported HTTP version");
            return -1;
        }
        tmp.method = dup_slice(raw, (size_t)(sp1 - raw));
        tmp.path = dup_slice(sp1 + 1, (size_t)(sp2 - sp1 - 1));
        if (!tmp.method || !tmp.path) {
            http_request_free(&tmp);
            set_error(out_status, out_error, 500, "internal server error");
            return -1;
        }
    }

    header_len = (size_t)(header_end - raw);
    content_header_state = parse_content_length_header(raw, header_len, &content_length);
    if (content_header_state < 0) {
        http_request_free(&tmp);
        set_error(out_status, out_error, 400, "invalid Content-Length header");
        return -1;
    }
    if (strcmp(tmp.method, "POST") == 0 && content_header_state == 0) {
        http_request_free(&tmp);
        set_error(out_status, out_error, 400, "Content-Length header is required");
        return -1;
    }

    cursor = header_end + 4;
    if (raw_len < (size_t)(cursor - raw) + content_length) {
        http_request_free(&tmp);
        set_error(out_status, out_error, 400, "request body is shorter than Content-Length");
        return -1;
    }

    tmp.content_length = content_length;
    tmp.body_length = content_length;
    tmp.body = dup_slice(cursor, content_length);
    if (content_length > 0 && !tmp.body) {
        http_request_free(&tmp);
        set_error(out_status, out_error, 500, "internal server error");
        return -1;
    }

    if (content_length > 0 && extract_sql_field(tmp.body, tmp.body_length, &tmp.sql) != 0) {
        http_request_free(&tmp);
        set_error(out_status, out_error, 400, "invalid JSON body");
        return -1;
    }

    *out = tmp;
    return 0;
}

void http_request_free(HttpRequest *request) {
    if (!request) {
        return;
    }
    free(request->method);
    free(request->path);
    free(request->body);
    free(request->sql);
    memset(request, 0, sizeof *request);
}
