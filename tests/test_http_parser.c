#include "http_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

static int write_all(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n <= 0) {
            return -1;
        }
        written += (size_t)n;
    }
    return 0;
}

static int test_read_and_parse(void) {
    int fds[2];
    const char *body = "{\"sql\":\"SELECT * FROM users;\"}";
    char request[512];
    char *raw = NULL;
    size_t raw_len = 0;
    int status = 0;
    char *error = NULL;
    HttpRequest parsed = {0};

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return fail("socketpair");
    }

    snprintf(request, sizeof request,
             "POST /query HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(body), body);

    if (write_all(fds[0], request, strlen(request)) != 0) {
        close(fds[0]);
        close(fds[1]);
        return fail("write request");
    }
    shutdown(fds[0], SHUT_WR);

    if (http_parser_read_request(fds[1], &raw, &raw_len, &status, &error) != 0) {
        close(fds[0]);
        close(fds[1]);
        free(error);
        return fail("read request");
    }
    if (http_parser_parse_request(raw, raw_len, &parsed, &status, &error) != 0) {
        close(fds[0]);
        close(fds[1]);
        free(raw);
        free(error);
        return fail("parse request");
    }

    if (strcmp(parsed.method, "POST") != 0 || strcmp(parsed.path, "/query") != 0 ||
        strcmp(parsed.sql, "SELECT * FROM users;") != 0) {
        http_request_free(&parsed);
        free(raw);
        close(fds[0]);
        close(fds[1]);
        return fail("parsed fields");
    }

    http_request_free(&parsed);
    free(raw);
    close(fds[0]);
    close(fds[1]);
    return 0;
}

static int test_invalid_json(void) {
    const char *body = "{\"sql\":123}";
    char request[256];
    HttpRequest parsed = {0};
    int status = 0;
    char *error = NULL;

    snprintf(request, sizeof request,
             "POST /query HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(body), body);

    if (http_parser_parse_request(request, strlen(request), &parsed, &status, &error) == 0) {
        http_request_free(&parsed);
        free(error);
        return fail("invalid json accepted");
    }
    if (status != 400 || !error || strstr(error, "invalid JSON body") == NULL) {
        free(error);
        return fail("invalid json status");
    }

    free(error);
    return 0;
}

static int test_missing_content_length(void) {
    const char *request =
        "POST /query HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";
    HttpRequest parsed = {0};
    int status = 0;
    char *error = NULL;

    if (http_parser_parse_request(request, strlen(request), &parsed, &status, &error) == 0) {
        http_request_free(&parsed);
        free(error);
        return fail("missing length accepted");
    }
    if (status != 400 || !error || strstr(error, "Content-Length") == NULL) {
        free(error);
        return fail("missing length status");
    }

    free(error);
    return 0;
}

int main(void) {
    if (test_read_and_parse() != 0) {
        return 1;
    }
    if (test_invalid_json() != 0) {
        return 1;
    }
    if (test_missing_content_length() != 0) {
        return 1;
    }
    return 0;
}
