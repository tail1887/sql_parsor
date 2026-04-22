#include "api_server.h"

#include "engine_adapter.h"
#include "http_parser.h"
#include "response_builder.h"
#include "sql_processor.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define API_SERVER_DEFAULT_WORKERS 4U
#define API_SERVER_DEFAULT_QUEUE_CAPACITY 64U
#define API_SERVER_REQUEST_TIMEOUT_SEC 3

struct ApiServer {
    int listen_fd;
    uint16_t port;
    size_t worker_count;
    size_t queue_capacity;
    pthread_t accept_thread;
    pthread_t *workers;
    int *queue_fds;
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;
    int stopping;
    pthread_mutex_t mutex;
    pthread_cond_t queue_not_empty;
};

static void close_fd_if_open(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void send_json_response(int fd, int status_code, const char *message) {
    char *body = NULL;
    char *response = NULL;
    size_t response_len = 0;

    if (response_builder_build_error_json(message, &body) != 0) {
        static const char fallback[] =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 36\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"status\":\"error\",\"message\":\"error\"}";
        (void)send_all(fd, fallback, sizeof fallback - 1);
        return;
    }
    if (response_builder_build_http_response(status_code, body, &response, &response_len) == 0) {
        (void)send_all(fd, response, response_len);
    }

    free(body);
    free(response);
}

static int set_client_timeout(int fd) {
    struct timeval timeout;

    timeout.tv_sec = API_SERVER_REQUEST_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) != 0) {
        return -1;
    }
    return 0;
}

static void handle_client(int fd) {
    char *raw_request = NULL;
    size_t raw_len = 0;
    int status = 0;
    char *error = NULL;
    HttpRequest request = {0};
    SqlExecutionResult result = {0};
    char *body = NULL;
    char *response = NULL;
    size_t response_len = 0;

    (void)set_client_timeout(fd);

    if (http_parser_read_request(fd, &raw_request, &raw_len, &status, &error) != 0) {
        send_json_response(fd, status ? status : 400, error ? error : "invalid request");
        goto cleanup;
    }

    if (http_parser_parse_request(raw_request, raw_len, &request, &status, &error) != 0) {
        send_json_response(fd, status ? status : 400, error ? error : "invalid request");
        goto cleanup;
    }

    if (strcmp(request.path, "/query") != 0) {
        send_json_response(fd, 404, "not found");
        goto cleanup;
    }
    if (strcmp(request.method, "POST") != 0) {
        send_json_response(fd, 405, "method not allowed");
        goto cleanup;
    }
    if (!request.sql || request.sql[0] == '\0') {
        send_json_response(fd, 400, "sql is required");
        goto cleanup;
    }

    (void)engine_adapter_execute_sql(request.sql, &result);
    if (!result.message) {
        send_json_response(fd, 500, "internal server error");
        goto cleanup;
    }
    if (response_builder_build_result_json(&result, &body) != 0 ||
        response_builder_build_http_response(200, body, &response, &response_len) != 0) {
        send_json_response(fd, 500, "internal server error");
        goto cleanup;
    }
    (void)send_all(fd, response, response_len);

cleanup:
    free(raw_request);
    free(error);
    free(body);
    free(response);
    http_request_free(&request);
    sql_processor_free_result(&result);
}

static int queue_push(ApiServer *server, int client_fd) {
    int ok = 0;

    pthread_mutex_lock(&server->mutex);
    if (!server->stopping && server->queue_count < server->queue_capacity) {
        server->queue_fds[server->queue_tail] = client_fd;
        server->queue_tail = (server->queue_tail + 1) % server->queue_capacity;
        server->queue_count++;
        pthread_cond_signal(&server->queue_not_empty);
        ok = 1;
    }
    pthread_mutex_unlock(&server->mutex);

    return ok;
}

static int queue_pop(ApiServer *server) {
    int client_fd = -1;

    pthread_mutex_lock(&server->mutex);
    while (!server->stopping && server->queue_count == 0) {
        pthread_cond_wait(&server->queue_not_empty, &server->mutex);
    }
    if (server->queue_count > 0) {
        client_fd = server->queue_fds[server->queue_head];
        server->queue_head = (server->queue_head + 1) % server->queue_capacity;
        server->queue_count--;
    }
    pthread_mutex_unlock(&server->mutex);

    return client_fd;
}

static void *worker_main(void *arg) {
    ApiServer *server = (ApiServer *)arg;

    for (;;) {
        int client_fd = queue_pop(server);
        if (client_fd < 0) {
            pthread_mutex_lock(&server->mutex);
            if (server->stopping && server->queue_count == 0) {
                pthread_mutex_unlock(&server->mutex);
                break;
            }
            pthread_mutex_unlock(&server->mutex);
            continue;
        }
        handle_client(client_fd);
        close_fd_if_open(client_fd);
    }

    return NULL;
}

static void *accept_main(void *arg) {
    ApiServer *server = (ApiServer *)arg;

    for (;;) {
        int client_fd = accept(server->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            pthread_mutex_lock(&server->mutex);
            if (server->stopping) {
                pthread_mutex_unlock(&server->mutex);
                break;
            }
            pthread_mutex_unlock(&server->mutex);
            continue;
        }

        if (!queue_push(server, client_fd)) {
            send_json_response(client_fd, 503, "server is busy");
            close_fd_if_open(client_fd);
        }
    }

    return NULL;
}

int api_server_start(ApiServer **out_server, const ApiServerConfig *config) {
    ApiServer *server = NULL;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof addr;
    int enable = 1;
    size_t worker_count = 0;
    size_t queue_capacity = 0;

    if (!out_server) {
        return -1;
    }
    *out_server = NULL;

    worker_count = (config && config->worker_count > 0) ? config->worker_count : API_SERVER_DEFAULT_WORKERS;
    queue_capacity =
        (config && config->queue_capacity > 0) ? config->queue_capacity : API_SERVER_DEFAULT_QUEUE_CAPACITY;

    server = calloc(1, sizeof *server);
    if (!server) {
        return -1;
    }

    server->listen_fd = -1;
    server->worker_count = worker_count;
    server->queue_capacity = queue_capacity;
    server->workers = calloc(worker_count, sizeof *server->workers);
    server->queue_fds = calloc(queue_capacity, sizeof *server->queue_fds);
    if (!server->workers || !server->queue_fds) {
        free(server->workers);
        free(server->queue_fds);
        free(server);
        return -1;
    }

    pthread_mutex_init(&server->mutex, NULL);
    pthread_cond_init(&server->queue_not_empty, NULL);

    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        api_server_destroy(server);
        return -1;
    }
    if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof enable) != 0) {
        api_server_destroy(server);
        return -1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(config ? config->port : 0);

    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        api_server_destroy(server);
        return -1;
    }
    if (listen(server->listen_fd, (int)queue_capacity) != 0) {
        api_server_destroy(server);
        return -1;
    }
    if (getsockname(server->listen_fd, (struct sockaddr *)&addr, &addr_len) != 0) {
        api_server_destroy(server);
        return -1;
    }
    server->port = ntohs(addr.sin_port);

    for (size_t i = 0; i < worker_count; i++) {
        if (pthread_create(&server->workers[i], NULL, worker_main, server) != 0) {
            api_server_stop(server);
            api_server_destroy(server);
            return -1;
        }
    }

    if (pthread_create(&server->accept_thread, NULL, accept_main, server) != 0) {
        api_server_stop(server);
        api_server_destroy(server);
        return -1;
    }

    *out_server = server;
    return 0;
}

void api_server_stop(ApiServer *server) {
    if (!server) {
        return;
    }

    pthread_mutex_lock(&server->mutex);
    server->stopping = 1;
    pthread_cond_broadcast(&server->queue_not_empty);
    pthread_mutex_unlock(&server->mutex);

    if (server->listen_fd >= 0) {
        shutdown(server->listen_fd, SHUT_RDWR);
        close_fd_if_open(server->listen_fd);
        server->listen_fd = -1;
    }

    if (server->accept_thread) {
        pthread_join(server->accept_thread, NULL);
        server->accept_thread = 0;
    }
    for (size_t i = 0; i < server->worker_count; i++) {
        if (server->workers[i]) {
            pthread_join(server->workers[i], NULL);
            server->workers[i] = 0;
        }
    }
}

void api_server_destroy(ApiServer *server) {
    if (!server) {
        return;
    }

    api_server_stop(server);
    for (size_t i = 0; i < server->queue_count; i++) {
        size_t index = (server->queue_head + i) % server->queue_capacity;
        close_fd_if_open(server->queue_fds[index]);
    }
    pthread_cond_destroy(&server->queue_not_empty);
    pthread_mutex_destroy(&server->mutex);
    free(server->workers);
    free(server->queue_fds);
    free(server);
}

uint16_t api_server_port(const ApiServer *server) { return server ? server->port : 0; }
