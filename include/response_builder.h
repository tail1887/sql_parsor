#ifndef RESPONSE_BUILDER_H
#define RESPONSE_BUILDER_H

#include "sql_result.h"

#include <stddef.h>

int response_builder_build_result_json(const SqlExecutionResult *result, char **out_body);
int response_builder_build_error_json(const char *message, char **out_body);
int response_builder_build_http_response(int status_code, const char *json_body, char **out_response,
                                         size_t *out_len);

#endif
