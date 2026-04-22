#include "api_server.h"

#include "engine_adapter.h"
#include "http_parser.h"
#include "response_builder.h"
#include "sql_processor.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define API_SERVER_DEFAULT_WORKERS 4U
#define API_SERVER_DEFAULT_QUEUE_CAPACITY 64U
#define API_SERVER_REQUEST_TIMEOUT_SEC 3

typedef struct {
    int client_fd;
    int64_t accepted_at_ns;
    int64_t socket_enqueued_at_ns;
} AcceptedSocket;

typedef struct {
    int client_fd;
    HttpRequest request;
    int64_t accepted_at_ns;
    int64_t socket_enqueued_at_ns;
    int64_t socket_dequeued_at_ns;
    int64_t parse_done_at_ns;
    int64_t request_enqueued_at_ns;
    int64_t request_dequeued_at_ns;
} RequestTask;

struct ApiServer {
    int listen_fd;
    uint16_t port;
    size_t worker_count;
    size_t io_thread_count;
    size_t queue_capacity;
    pthread_t accept_thread;
    pthread_t *io_threads;
    pthread_t *workers;

    AcceptedSocket **socket_queue;
    size_t socket_queue_head;
    size_t socket_queue_tail;
    size_t socket_queue_count;
    pthread_mutex_t socket_mutex;
    pthread_cond_t socket_not_empty;

    RequestTask **request_queue;
    size_t request_queue_head;
    size_t request_queue_tail;
    size_t request_queue_count;
    pthread_mutex_t request_mutex;
    pthread_cond_t request_not_empty;

    int stopping;
};

static int64_t monotonic_now_ns(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

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

static int metrics_enabled(void) {
    static int initialized = 0;
    static int enabled = 0;

    if (!initialized) {
        const char *value = getenv("SQL_API_SERVER_METRICS");
        enabled = (value && value[0] != '\0' && strcmp(value, "0") != 0) ? 1 : 0;
        initialized = 1;
    }
    return enabled;
}

static unsigned long test_exec_delay_ms(void) {
    const char *value = getenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
    char *end = NULL;
    unsigned long delay_ms = 0;

    if (value && value[0] != '\0') {
        delay_ms = strtoul(value, &end, 10);
        if (!end || *end != '\0') {
            delay_ms = 0;
        }
    }
    return delay_ms;
}

static void sleep_ms(unsigned long delay_ms) {
    struct timespec req;

    if (delay_ms == 0) {
        return;
    }

    req.tv_sec = (time_t)(delay_ms / 1000UL);
    req.tv_nsec = (long)((delay_ms % 1000UL) * 1000000UL);
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

static void accepted_socket_destroy(AcceptedSocket *accepted) {
    free(accepted);
}

static void request_task_destroy(RequestTask *task) {
    if (!task) {
        return;
    }
    http_request_free(&task->request);
    free(task);
}

static int validate_request(const HttpRequest *request, int *out_status, const char **out_message) {
    if (!request || !out_status || !out_message) {
        return -1;
    }

    if (!request->path || strcmp(request->path, "/query") != 0) {
        *out_status = 404;
        *out_message = "not found";
        return -1;
    }
    if (!request->method || strcmp(request->method, "POST") != 0) {
        *out_status = 405;
        *out_message = "method not allowed";
        return -1;
    }
    if (!request->sql || request->sql[0] == '\0') {
        *out_status = 400;
        *out_message = "sql is required";
        return -1;
    }

    return 0;
}

static RequestTask *prepare_request_task(AcceptedSocket *accepted) {
    RequestTask *task = NULL;
    char *raw_request = NULL;
    size_t raw_len = 0;
    int status = 0;
    char *error = NULL;
    const char *validation_message = NULL;

    if (!accepted) {
        return NULL;
    }

    task = calloc(1, sizeof *task);
    if (!task) {
        send_json_response(accepted->client_fd, 500, "internal server error");
        close_fd_if_open(accepted->client_fd);
        return NULL;
    }

    task->client_fd = accepted->client_fd;
    task->accepted_at_ns = accepted->accepted_at_ns;
    task->socket_enqueued_at_ns = accepted->socket_enqueued_at_ns;
    task->socket_dequeued_at_ns = monotonic_now_ns();

    if (set_client_timeout(task->client_fd) != 0) {
        send_json_response(task->client_fd, 500, "internal server error");
        close_fd_if_open(task->client_fd);
        request_task_destroy(task);
        return NULL;
    }

    if (http_parser_read_request(task->client_fd, &raw_request, &raw_len, &status, &error) != 0) {
        send_json_response(task->client_fd, status ? status : 400, error ? error : "invalid request");
        close_fd_if_open(task->client_fd);
        free(raw_request);
        free(error);
        request_task_destroy(task);
        return NULL;
    }

    if (http_parser_parse_request(raw_request, raw_len, &task->request, &status, &error) != 0) {
        send_json_response(task->client_fd, status ? status : 400, error ? error : "invalid request");
        close_fd_if_open(task->client_fd);
        free(raw_request);
        free(error);
        request_task_destroy(task);
        return NULL;
    }

    if (validate_request(&task->request, &status, &validation_message) != 0) {
        send_json_response(task->client_fd, status, validation_message);
        close_fd_if_open(task->client_fd);
        free(raw_request);
        free(error);
        request_task_destroy(task);
        return NULL;
    }

    task->parse_done_at_ns = monotonic_now_ns();
    free(raw_request);
    free(error);
    return task;
}

static void log_request_metrics(const RequestTask *task, const EngineExecutionStats *stats, int64_t response_done_ns) {
    int64_t socket_queue_wait_ns = 0;
    int64_t read_parse_ns = 0;
    int64_t request_queue_wait_ns = 0;
    int64_t total_queue_wait_ns = 0;
    int64_t lock_wait_ns = 0;
    int64_t sql_exec_ns = 0;
    int64_t response_total_ns = 0;

    if (!task || !metrics_enabled()) {
        return;
    }

    if (task->socket_dequeued_at_ns > 0) {
        socket_queue_wait_ns = task->socket_dequeued_at_ns - task->socket_enqueued_at_ns;
    }
    if (task->parse_done_at_ns > 0) {
        read_parse_ns = task->parse_done_at_ns - task->socket_dequeued_at_ns;
    }
    if (task->request_dequeued_at_ns > 0) {
        request_queue_wait_ns = task->request_dequeued_at_ns - task->request_enqueued_at_ns;
    }
    total_queue_wait_ns = socket_queue_wait_ns + request_queue_wait_ns;
    if (stats) {
        lock_wait_ns = stats->lock_wait_ns;
        sql_exec_ns = stats->execute_ns;
    }
    response_total_ns = response_done_ns - task->accepted_at_ns;

    fprintf(stderr,
            "api metrics: total_queue_wait_ns=%lld read_parse_ns=%lld lock_wait_ns=%lld sql_exec_ns=%lld "
            "response_total_ns=%lld\n",
            (long long)total_queue_wait_ns, (long long)read_parse_ns, (long long)lock_wait_ns,
            (long long)sql_exec_ns, (long long)response_total_ns);
}

static void handle_request_task(RequestTask *task) {
    SqlExecutionResult result = {0};
    EngineExecutionStats stats = {0};
    char *body = NULL;
    char *response = NULL;
    size_t response_len = 0;
    int64_t response_done_ns = 0;

    if (!task) {
        return;
    }

    sleep_ms(test_exec_delay_ms());

    (void)engine_adapter_execute_sql_with_stats(task->request.sql, &result, &stats);
    if (!result.message) {
        send_json_response(task->client_fd, 500, "internal server error");
        response_done_ns = monotonic_now_ns();
        log_request_metrics(task, &stats, response_done_ns);
        goto cleanup;
    }
    if (response_builder_build_result_json(&result, &body) != 0 ||
        response_builder_build_http_response(200, body, &response, &response_len) != 0) {
        send_json_response(task->client_fd, 500, "internal server error");
        response_done_ns = monotonic_now_ns();
        log_request_metrics(task, &stats, response_done_ns);
        goto cleanup;
    }
    (void)send_all(task->client_fd, response, response_len);
    response_done_ns = monotonic_now_ns();
    log_request_metrics(task, &stats, response_done_ns);

cleanup:
    free(body);
    free(response);
    sql_processor_free_result(&result);
}

static int socket_queue_push(ApiServer *server, AcceptedSocket *accepted) {
    int ok = 0;

    pthread_mutex_lock(&server->socket_mutex);
    if (!server->stopping && server->socket_queue_count < server->queue_capacity) {
        server->socket_queue[server->socket_queue_tail] = accepted;
        server->socket_queue_tail = (server->socket_queue_tail + 1) % server->queue_capacity;
        server->socket_queue_count++;
        pthread_cond_signal(&server->socket_not_empty);
        ok = 1;
    }
    pthread_mutex_unlock(&server->socket_mutex);

    return ok;
}

static AcceptedSocket *socket_queue_pop(ApiServer *server) {
    AcceptedSocket *accepted = NULL;

    pthread_mutex_lock(&server->socket_mutex);
    while (!server->stopping && server->socket_queue_count == 0) {
        pthread_cond_wait(&server->socket_not_empty, &server->socket_mutex);
    }
    if (server->socket_queue_count > 0) {
        accepted = server->socket_queue[server->socket_queue_head];
        server->socket_queue[server->socket_queue_head] = NULL;
        server->socket_queue_head = (server->socket_queue_head + 1) % server->queue_capacity;
        server->socket_queue_count--;
    }
    pthread_mutex_unlock(&server->socket_mutex);

    return accepted;
}

static int request_queue_push(ApiServer *server, RequestTask *task) {
    int ok = 0;

    pthread_mutex_lock(&server->request_mutex);
    if (!server->stopping && server->request_queue_count < server->queue_capacity) {
        task->request_enqueued_at_ns = monotonic_now_ns();
        server->request_queue[server->request_queue_tail] = task;
        server->request_queue_tail = (server->request_queue_tail + 1) % server->queue_capacity;
        server->request_queue_count++;
        pthread_cond_signal(&server->request_not_empty);
        ok = 1;
    }
    pthread_mutex_unlock(&server->request_mutex);

    return ok;
}

static RequestTask *request_queue_pop(ApiServer *server) {
    RequestTask *task = NULL;

    pthread_mutex_lock(&server->request_mutex);
    while (!server->stopping && server->request_queue_count == 0) {
        pthread_cond_wait(&server->request_not_empty, &server->request_mutex);
    }
    if (server->request_queue_count > 0) {
        task = server->request_queue[server->request_queue_head];
        server->request_queue[server->request_queue_head] = NULL;
        server->request_queue_head = (server->request_queue_head + 1) % server->queue_capacity;
        server->request_queue_count--;
    }
    pthread_mutex_unlock(&server->request_mutex);

    return task;
}

static void *io_main(void *arg) {
    ApiServer *server = (ApiServer *)arg;

    for (;;) {
        AcceptedSocket *accepted = socket_queue_pop(server);
        RequestTask *task = NULL;

        if (!accepted) {
            pthread_mutex_lock(&server->socket_mutex);
            if (server->stopping && server->socket_queue_count == 0) {
                pthread_mutex_unlock(&server->socket_mutex);
                break;
            }
            pthread_mutex_unlock(&server->socket_mutex);
            continue;
        }

        task = prepare_request_task(accepted);
        accepted_socket_destroy(accepted);
        if (!task) {
            continue;
        }

        if (!request_queue_push(server, task)) {
            send_json_response(task->client_fd, 503, "server is busy");
            close_fd_if_open(task->client_fd);
            request_task_destroy(task);
        }
    }

    return NULL;
}

static void *worker_main(void *arg) {
    ApiServer *server = (ApiServer *)arg;

    for (;;) {
        RequestTask *task = request_queue_pop(server);

        if (!task) {
            pthread_mutex_lock(&server->request_mutex);
            if (server->stopping && server->request_queue_count == 0) {
                pthread_mutex_unlock(&server->request_mutex);
                break;
            }
            pthread_mutex_unlock(&server->request_mutex);
            continue;
        }

        task->request_dequeued_at_ns = monotonic_now_ns();
        handle_request_task(task);
        close_fd_if_open(task->client_fd);
        request_task_destroy(task);
    }

    return NULL;
}

static void *accept_main(void *arg) {
    ApiServer *server = (ApiServer *)arg;

    for (;;) {
        int client_fd = accept(server->listen_fd, NULL, NULL);
        AcceptedSocket *accepted = NULL;

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            pthread_mutex_lock(&server->socket_mutex);
            if (server->stopping) {
                pthread_mutex_unlock(&server->socket_mutex);
                break;
            }
            pthread_mutex_unlock(&server->socket_mutex);
            continue;
        }

        accepted = calloc(1, sizeof *accepted);
        if (!accepted) {
            send_json_response(client_fd, 500, "internal server error");
            close_fd_if_open(client_fd);
            continue;
        }

        accepted->client_fd = client_fd;
        accepted->accepted_at_ns = monotonic_now_ns();
        accepted->socket_enqueued_at_ns = accepted->accepted_at_ns;

        if (!socket_queue_push(server, accepted)) {
            send_json_response(client_fd, 503, "server is busy");
            close_fd_if_open(client_fd);
            accepted_socket_destroy(accepted);
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
    server->io_thread_count = worker_count;
    server->queue_capacity = queue_capacity;
    server->io_threads = calloc(server->io_thread_count, sizeof *server->io_threads);
    server->workers = calloc(worker_count, sizeof *server->workers);
    server->socket_queue = calloc(queue_capacity, sizeof *server->socket_queue);
    server->request_queue = calloc(queue_capacity, sizeof *server->request_queue);
    if (!server->io_threads || !server->workers || !server->socket_queue || !server->request_queue) {
        free(server->io_threads);
        free(server->workers);
        free(server->socket_queue);
        free(server->request_queue);
        free(server);
        return -1;
    }

    pthread_mutex_init(&server->socket_mutex, NULL);
    pthread_cond_init(&server->socket_not_empty, NULL);
    pthread_mutex_init(&server->request_mutex, NULL);
    pthread_cond_init(&server->request_not_empty, NULL);

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

    for (size_t i = 0; i < server->io_thread_count; i++) {
        if (pthread_create(&server->io_threads[i], NULL, io_main, server) != 0) {
            api_server_stop(server);
            api_server_destroy(server);
            return -1;
        }
    }

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

    pthread_mutex_lock(&server->socket_mutex);
    server->stopping = 1;
    pthread_cond_broadcast(&server->socket_not_empty);
    pthread_mutex_unlock(&server->socket_mutex);

    pthread_mutex_lock(&server->request_mutex);
    pthread_cond_broadcast(&server->request_not_empty);
    pthread_mutex_unlock(&server->request_mutex);

    if (server->listen_fd >= 0) {
        shutdown(server->listen_fd, SHUT_RDWR);
        close_fd_if_open(server->listen_fd);
        server->listen_fd = -1;
    }

    if (server->accept_thread) {
        pthread_join(server->accept_thread, NULL);
        server->accept_thread = 0;
    }

    for (size_t i = 0; i < server->io_thread_count; i++) {
        if (server->io_threads[i]) {
            pthread_join(server->io_threads[i], NULL);
            server->io_threads[i] = 0;
        }
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

    for (size_t i = 0; i < server->socket_queue_count; i++) {
        size_t index = (server->socket_queue_head + i) % server->queue_capacity;
        AcceptedSocket *accepted = server->socket_queue[index];
        if (!accepted) {
            continue;
        }
        close_fd_if_open(accepted->client_fd);
        accepted_socket_destroy(accepted);
    }

    for (size_t i = 0; i < server->request_queue_count; i++) {
        size_t index = (server->request_queue_head + i) % server->queue_capacity;
        RequestTask *task = server->request_queue[index];
        if (!task) {
            continue;
        }
        close_fd_if_open(task->client_fd);
        request_task_destroy(task);
    }

    pthread_cond_destroy(&server->socket_not_empty);
    pthread_mutex_destroy(&server->socket_mutex);
    pthread_cond_destroy(&server->request_not_empty);
    pthread_mutex_destroy(&server->request_mutex);
    free(server->io_threads);
    free(server->workers);
    free(server->socket_queue);
    free(server->request_queue);
    free(server);
}

uint16_t api_server_port(const ApiServer *server) { return server ? server->port : 0; }
