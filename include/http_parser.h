#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>

typedef struct {
    char *method;
    char *path;
    char *body;
    size_t body_length;
    size_t content_length;
    char *sql;
} HttpRequest;

int http_parser_read_request(int fd, char **out_raw, size_t *out_len, int *out_status, char **out_error);
int http_parser_parse_request(const char *raw, size_t raw_len, HttpRequest *out, int *out_status,
                              char **out_error);
void http_request_free(HttpRequest *request);

#endif
