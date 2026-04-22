#include "week8/api_server.h"
#include "week8/engine_bridge.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct RequestContext {
    int client_fd;
    int timeout_ms;
    struct timespec deadline;
    _Atomic int cancel_token;
    struct RequestContext *next_all;
} RequestContext;

typedef struct {
    RequestContext *ctx;
} PerRequestThreadArg;

static Week8DispatchMode resolve_dispatch_mode(void) {
    const char *mode = getenv("W8_DISPATCH_MODE");
    if (!mode) return WEEK8_DISPATCH_POOL;
    if (strcmp(mode, "per_request") == 0) return WEEK8_DISPATCH_PER_REQUEST;
    return WEEK8_DISPATCH_POOL;
}

static Week8TimeoutPolicy resolve_timeout_policy(void) {
    const char *policy = getenv("W8_TIMEOUT_POLICY");
    if (!policy) return WEEK8_TIMEOUT_DYNAMIC;
    if (strcmp(policy, "fixed") == 0) return WEEK8_TIMEOUT_FIXED;
    return WEEK8_TIMEOUT_DYNAMIC;
}

static int resolve_fixed_timeout_ms(void) {
    const char *s = getenv("W8_FIXED_TIMEOUT_MS");
    if (!s || s[0] == '\0') return 1200;
    long v = strtol(s, NULL, 10);
    if (v < 1) return 1;
    if (v > 60000) return 60000;
    return (int)v;
}

static struct timespec now_realtime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts;
}

static struct timespec add_ms(struct timespec base, int ms) {
    base.tv_sec += ms / 1000;
    base.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (base.tv_nsec >= 1000000000L) {
        base.tv_sec += 1;
        base.tv_nsec -= 1000000000L;
    }
    return base;
}

static int cmp_timespec(struct timespec a, struct timespec b) {
    if (a.tv_sec != b.tv_sec) return (a.tv_sec < b.tv_sec) ? -1 : 1;
    if (a.tv_nsec != b.tv_nsec) return (a.tv_nsec < b.tv_nsec) ? -1 : 1;
    return 0;
}

static int compute_dynamic_timeout_ms(int queue_size, int queue_capacity) {
    const int max_ms = 4000;
    const int min_ms = 1200;
    if (queue_capacity <= 0) return max_ms;
    if (queue_size < 0) queue_size = 0;
    if (queue_size > queue_capacity) queue_size = queue_capacity;
    int span = max_ms - min_ms;
    int decay = (span * queue_size) / queue_capacity;
    int timeout_ms = max_ms - decay;
    if (timeout_ms < min_ms) timeout_ms = min_ms;
    return timeout_ms;
}

static void registry_add(Week8ApiServer *server, RequestContext *ctx) {
    pthread_mutex_lock(&server->registry_mu);
    RequestContext **head = (RequestContext **)&server->active_requests;
    ctx->next_all = *head;
    *head = ctx;
    pthread_cond_signal(&server->registry_cv);
    pthread_mutex_unlock(&server->registry_mu);
}

static void registry_remove(Week8ApiServer *server, RequestContext *ctx) {
    pthread_mutex_lock(&server->registry_mu);
    RequestContext **head = (RequestContext **)&server->active_requests;
    RequestContext **pp = head;
    while (*pp) {
        if (*pp == ctx) {
            *pp = ctx->next_all;
            break;
        }
        pp = &(*pp)->next_all;
    }
    pthread_mutex_unlock(&server->registry_mu);
}

static int write_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void send_json_response(int client_fd, int status, const char *status_text, const char *json_body) {
    char header[256];
    int body_len = (int)strlen(json_body);
    int n = snprintf(header,
                     sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status,
                     status_text,
                     body_len);
    if (n <= 0 || (size_t)n >= sizeof(header)) return;
    if (write_all(client_fd, header, (size_t)n) != 0) return;
    (void)write_all(client_fd, json_body, (size_t)body_len);
}

static char *json_escape_dup(const char *s) {
    if (!s) {
        char *empty = (char *)calloc(1, 1);
        return empty;
    }
    size_t n = strlen(s);
    size_t cap = n * 2 + 1;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (j + 3 >= cap) {
            cap *= 2;
            char *grown = (char *)realloc(out, cap);
            if (!grown) {
                free(out);
                return NULL;
            }
            out = grown;
        }
        switch (c) {
            case '\\':
                out[j++] = '\\';
                out[j++] = '\\';
                break;
            case '"':
                out[j++] = '\\';
                out[j++] = '"';
                break;
            case '\n':
                out[j++] = '\\';
                out[j++] = 'n';
                break;
            case '\r':
                out[j++] = '\\';
                out[j++] = 'r';
                break;
            case '\t':
                out[j++] = '\\';
                out[j++] = 't';
                break;
            default:
                out[j++] = (char)c;
                break;
        }
    }
    out[j] = '\0';
    return out;
}

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static int extract_sql_field(const char *json, char *sql_out, size_t sql_out_cap) {
    const char *key = "\"sql\"";
    const char *p = strstr(json, key);
    if (!p) return -1;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return -1;
    p++;

    size_t j = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (*p == 'n')
                sql_out[j++] = '\n';
            else if (*p == 'r')
                sql_out[j++] = '\r';
            else if (*p == 't')
                sql_out[j++] = '\t';
            else if (*p == '"' || *p == '\\')
                sql_out[j++] = *p;
            else
                return -1;
            p++;
            if (j + 1 >= sql_out_cap) return -1;
            continue;
        }
        sql_out[j++] = *p++;
        if (j + 1 >= sql_out_cap) return -1;
    }
    if (*p != '"') return -1;
    sql_out[j] = '\0';
    if (j == 0) return -1;
    return 0;
}

static void handle_query_request(int client_fd, const char *body, RequestContext *ctx) {
    char sql[4096];
    if (ctx && atomic_load(&ctx->cancel_token)) {
        send_json_response(client_fd,
                           504,
                           "Gateway Timeout",
                           "{\"ok\":false,\"error\":{\"code\":\"TIMEOUT\",\"message\":\"deadline exceeded\",\"retryable\":true}}");
        return;
    }
    if (!body || extract_sql_field(body, sql, sizeof(sql)) != 0) {
        send_json_response(client_fd,
                           400,
                           "Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"INVALID_REQUEST\",\"message\":\"invalid json or missing sql\",\"retryable\":false}}");
        return;
    }

    Week8EngineResult result;
    if (week8_engine_bridge_execute_sql(sql, &result) != 0) {
        send_json_response(client_fd,
                           500,
                           "Internal Server Error",
                           "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"bridge execute failed\",\"retryable\":false}}");
        return;
    }
    if (ctx && atomic_load(&ctx->cancel_token)) {
        week8_engine_bridge_result_free(&result);
        send_json_response(client_fd,
                           504,
                           "Gateway Timeout",
                           "{\"ok\":false,\"error\":{\"code\":\"TIMEOUT\",\"message\":\"deadline exceeded\",\"retryable\":true}}");
        return;
    }

    if (result.status == WEEK8_ENGINE_OK) {
        char *escaped_out = json_escape_dup(result.stdout_text ? result.stdout_text : "");
        if (!escaped_out) {
            week8_engine_bridge_result_free(&result);
            send_json_response(client_fd,
                               500,
                               "Internal Server Error",
                               "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"response encode failed\",\"retryable\":false}}");
            return;
        }
        size_t need = strlen(escaped_out) + 128;
        char *resp = (char *)malloc(need);
        if (!resp) {
            free(escaped_out);
            week8_engine_bridge_result_free(&result);
            send_json_response(client_fd,
                               500,
                               "Internal Server Error",
                               "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"out of memory\",\"retryable\":false}}");
            return;
        }
        snprintf(resp, need, "{\"ok\":true,\"data\":{\"output\":\"%s\"},\"metadata\":{\"endpoint\":\"/query\"}}", escaped_out);
        send_json_response(client_fd, 200, "OK", resp);
        free(resp);
        free(escaped_out);
        week8_engine_bridge_result_free(&result);
        return;
    }

    if (result.status == WEEK8_ENGINE_PARSE_ERR) {
        char *escaped_err = json_escape_dup(result.stderr_text ? result.stderr_text : "parse error");
        if (!escaped_err) escaped_err = dup_cstr("parse error");
        size_t need = strlen(escaped_err) + 160;
        char *resp = (char *)malloc(need);
        if (resp) {
            snprintf(resp,
                     need,
                     "{\"ok\":false,\"error\":{\"code\":\"SQL_PARSE_ERROR\",\"message\":\"%s\",\"retryable\":false}}",
                     escaped_err);
            send_json_response(client_fd, 422, "Unprocessable Entity", resp);
            free(resp);
        } else {
            send_json_response(client_fd,
                               422,
                               "Unprocessable Entity",
                               "{\"ok\":false,\"error\":{\"code\":\"SQL_PARSE_ERROR\",\"message\":\"parse error\",\"retryable\":false}}");
        }
        free(escaped_err);
        week8_engine_bridge_result_free(&result);
        return;
    }

    {
        char *escaped_err = json_escape_dup(result.stderr_text ? result.stderr_text : "exec error");
        if (!escaped_err) escaped_err = dup_cstr("exec error");
        size_t need = strlen(escaped_err) + 160;
        char *resp = (char *)malloc(need);
        if (resp) {
            snprintf(resp,
                     need,
                     "{\"ok\":false,\"error\":{\"code\":\"SQL_EXEC_ERROR\",\"message\":\"%s\",\"retryable\":false}}",
                     escaped_err);
            send_json_response(client_fd, 409, "Conflict", resp);
            free(resp);
        } else {
            send_json_response(client_fd,
                               409,
                               "Conflict",
                               "{\"ok\":false,\"error\":{\"code\":\"SQL_EXEC_ERROR\",\"message\":\"exec error\",\"retryable\":false}}");
        }
        free(escaped_err);
        week8_engine_bridge_result_free(&result);
    }
}

static void handle_client(int client_fd, RequestContext *ctx) {
    char req[2048];
    ssize_t n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';
    if (ctx && atomic_load(&ctx->cancel_token)) {
        send_json_response(client_fd,
                           504,
                           "Gateway Timeout",
                           "{\"ok\":false,\"error\":{\"code\":\"TIMEOUT\",\"message\":\"deadline exceeded\",\"retryable\":true}}");
        return;
    }

    if (strncmp(req, "GET /health ", 12) == 0) {
        send_json_response(client_fd,
                           200,
                           "OK",
                           "{\"ok\":true,\"data\":{\"status\":\"ok\"},\"metadata\":{\"service\":\"sql_parsor_week8_api\"}}");
        return;
    }

    if (strncmp(req, "POST /query ", 12) == 0) {
        const char *body = strstr(req, "\r\n\r\n");
        if (!body) {
            send_json_response(client_fd,
                               400,
                               "Bad Request",
                               "{\"ok\":false,\"error\":{\"code\":\"INVALID_REQUEST\",\"message\":\"missing request body\",\"retryable\":false}}");
            return;
        }
        body += 4;
        handle_query_request(client_fd, body, ctx);
        return;
    }

    send_json_response(client_fd,
                       404,
                       "Not Found",
                       "{\"ok\":false,\"error\":{\"code\":\"NOT_FOUND\",\"message\":\"route not found\",\"retryable\":false}}");
}

static int queue_push(Week8ApiServer *server, RequestContext *ctx) {
    pthread_mutex_lock(&server->queue_mu);
    if (server->queue_size >= server->queue_capacity) {
        pthread_mutex_unlock(&server->queue_mu);
        return -1;
    }
    server->client_queue[server->queue_tail] = ctx;
    server->queue_tail = (server->queue_tail + 1) % server->queue_capacity;
    server->queue_size++;
    pthread_cond_signal(&server->queue_not_empty);
    pthread_mutex_unlock(&server->queue_mu);
    return 0;
}

static int queue_pop(Week8ApiServer *server, RequestContext **ctx) {
    pthread_mutex_lock(&server->queue_mu);
    while (server->queue_size == 0 && !server->stop_requested) {
        pthread_cond_wait(&server->queue_not_empty, &server->queue_mu);
    }
    if (server->queue_size == 0 && server->stop_requested) {
        pthread_mutex_unlock(&server->queue_mu);
        return -1;
    }
    *ctx = (RequestContext *)server->client_queue[server->queue_head];
    server->queue_head = (server->queue_head + 1) % server->queue_capacity;
    server->queue_size--;
    pthread_cond_signal(&server->queue_not_full);
    pthread_mutex_unlock(&server->queue_mu);
    return 0;
}

static void *per_request_thread_loop(void *arg) {
    PerRequestThreadArg *thread_arg = (PerRequestThreadArg *)arg;
    if (!thread_arg) return NULL;
    RequestContext *ctx = thread_arg->ctx;
    if (ctx) {
        handle_client(ctx->client_fd, ctx);
        close(ctx->client_fd);
        free(ctx);
    }
    free(thread_arg);
    return NULL;
}

static int serve_per_request_mode(Week8ApiServer *server) {
    while (!server->stop_requested) {
        int client_fd = accept(server->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (server->stop_requested) break;
            if (errno == EINTR) continue;
            fprintf(server->err, "week8 api: accept() failed\n");
            return -1;
        }

        RequestContext *ctx = (RequestContext *)calloc(1, sizeof(RequestContext));
        if (!ctx) {
            send_json_response(client_fd,
                               500,
                               "Internal Server Error",
                               "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"out of memory\",\"retryable\":false}}");
            close(client_fd);
            continue;
        }
        ctx->client_fd = client_fd;

        PerRequestThreadArg *thread_arg = (PerRequestThreadArg *)calloc(1, sizeof(PerRequestThreadArg));
        if (!thread_arg) {
            send_json_response(client_fd,
                               500,
                               "Internal Server Error",
                               "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"out of memory\",\"retryable\":false}}");
            close(client_fd);
            free(ctx);
            continue;
        }
        thread_arg->ctx = ctx;

        pthread_t tid;
        if (pthread_create(&tid, NULL, per_request_thread_loop, thread_arg) != 0) {
            send_json_response(client_fd,
                               500,
                               "Internal Server Error",
                               "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"thread create failed\",\"retryable\":true}}");
            close(client_fd);
            free(ctx);
            free(thread_arg);
            continue;
        }
        (void)pthread_detach(tid);
    }
    return 0;
}

static void *worker_loop(void *arg) {
    Week8ApiServer *server = (Week8ApiServer *)arg;
    while (!server->stop_requested) {
        RequestContext *ctx = NULL;
        if (queue_pop(server, &ctx) != 0) break;
        if (!ctx) continue;
        handle_client(ctx->client_fd, ctx);
        close(ctx->client_fd);
        registry_remove(server, ctx);
        free(ctx);
    }
    return NULL;
}

static void *watcher_loop(void *arg) {
    Week8ApiServer *server = (Week8ApiServer *)arg;
    pthread_mutex_lock(&server->registry_mu);
    while (!server->stop_requested) {
        struct timespec now = now_realtime();
        struct timespec next_deadline = add_ms(now, 300);
        int has_deadline = 0;
        RequestContext *cur = (RequestContext *)server->active_requests;
        while (cur) {
            if (cmp_timespec(cur->deadline, now) <= 0) {
                atomic_store(&cur->cancel_token, 1);
            } else if (!has_deadline || cmp_timespec(cur->deadline, next_deadline) < 0) {
                next_deadline = cur->deadline;
                has_deadline = 1;
            }
            cur = cur->next_all;
        }
        if (server->stop_requested) break;
        if (has_deadline) {
            pthread_cond_timedwait(&server->registry_cv, &server->registry_mu, &next_deadline);
        } else {
            pthread_cond_wait(&server->registry_cv, &server->registry_mu);
        }
    }
    pthread_mutex_unlock(&server->registry_mu);
    return NULL;
}

static int init_worker_pool(Week8ApiServer *server) {
    int mu_inited = 0;
    int ne_inited = 0;
    int nf_inited = 0;
    int reg_mu_inited = 0;
    int reg_cv_inited = 0;
    int watcher_started = 0;

    if (pthread_mutex_init(&server->queue_mu, NULL) != 0) return -1;
    mu_inited = 1;
    if (pthread_cond_init(&server->queue_not_empty, NULL) != 0) {
        goto fail;
    }
    ne_inited = 1;
    if (pthread_cond_init(&server->queue_not_full, NULL) != 0) {
        goto fail;
    }
    nf_inited = 1;
    if (pthread_mutex_init(&server->registry_mu, NULL) != 0) goto fail;
    reg_mu_inited = 1;
    if (pthread_cond_init(&server->registry_cv, NULL) != 0) goto fail;
    reg_cv_inited = 1;

    server->client_queue = (void **)malloc((size_t)server->queue_capacity * sizeof(void *));
    if (!server->client_queue) goto fail;
    server->workers = (pthread_t *)malloc((size_t)server->worker_count * sizeof(pthread_t));
    if (!server->workers) goto fail;

    server->queue_head = 0;
    server->queue_tail = 0;
    server->queue_size = 0;
    server->active_requests = NULL;

    if (pthread_create(&server->watcher, NULL, watcher_loop, server) != 0) goto fail;
    watcher_started = 1;

    for (int i = 0; i < server->worker_count; ++i) {
        if (pthread_create(&server->workers[i], NULL, worker_loop, server) != 0) {
            server->stop_requested = 1;
            pthread_cond_broadcast(&server->queue_not_empty);
            for (int j = 0; j < i; ++j) {
                pthread_join(server->workers[j], NULL);
            }
            goto fail;
        }
    }
    return 0;

fail:
    if (watcher_started) {
        server->stop_requested = 1;
        pthread_cond_broadcast(&server->registry_cv);
        pthread_join(server->watcher, NULL);
    }
    free(server->workers);
    server->workers = NULL;
    free(server->client_queue);
    server->client_queue = NULL;
    if (nf_inited) pthread_cond_destroy(&server->queue_not_full);
    if (ne_inited) pthread_cond_destroy(&server->queue_not_empty);
    if (mu_inited) pthread_mutex_destroy(&server->queue_mu);
    if (reg_cv_inited) pthread_cond_destroy(&server->registry_cv);
    if (reg_mu_inited) pthread_mutex_destroy(&server->registry_mu);
    return -1;
}

static void destroy_worker_pool(Week8ApiServer *server) {
    if (!server) return;
    if (!server->pool_inited) return;
    server->stop_requested = 1;
    pthread_cond_broadcast(&server->queue_not_empty);
    pthread_cond_broadcast(&server->registry_cv);

    pthread_join(server->watcher, NULL);

    if (server->workers) {
        for (int i = 0; i < server->worker_count; ++i) {
            pthread_join(server->workers[i], NULL);
        }
        free(server->workers);
        server->workers = NULL;
    }

    if (server->client_queue) {
        while (server->queue_size > 0) {
            RequestContext *ctx = (RequestContext *)server->client_queue[server->queue_head];
            if (ctx) {
                close(ctx->client_fd);
            }
            server->queue_head = (server->queue_head + 1) % server->queue_capacity;
            server->queue_size--;
        }
        free(server->client_queue);
        server->client_queue = NULL;
    }

    {
        RequestContext *cur = (RequestContext *)server->active_requests;
        while (cur) {
            RequestContext *next = cur->next_all;
            free(cur);
            cur = next;
        }
        server->active_requests = NULL;
    }

    pthread_cond_destroy(&server->queue_not_full);
    pthread_cond_destroy(&server->queue_not_empty);
    pthread_mutex_destroy(&server->queue_mu);
    pthread_cond_destroy(&server->registry_cv);
    pthread_mutex_destroy(&server->registry_mu);
    server->pool_inited = 0;
}

int week8_api_server_init(Week8ApiServer *server, const Week8ApiServerConfig *config, FILE *err) {
    if (!server) return -1;
    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    server->err = err ? err : stderr;
    server->dispatch_mode = resolve_dispatch_mode();
    server->timeout_policy = resolve_timeout_policy();
    server->fixed_timeout_ms = resolve_fixed_timeout_ms();

    const char *host = "127.0.0.1";
    uint16_t port = 0;
    if (config) {
        if (config->host && config->host[0] != '\0') host = config->host;
        port = config->port;
        server->worker_count = config->worker_count;
        server->queue_capacity = config->queue_capacity;
    }
    if (server->worker_count <= 0) server->worker_count = 4;
    if (server->queue_capacity <= 0) server->queue_capacity = 64;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(server->err, "week8 api: socket() failed\n");
        return -1;
    }

    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        fprintf(server->err, "week8 api: setsockopt(SO_REUSEADDR) failed\n");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(server->err, "week8 api: invalid host '%s'\n", host);
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(server->err, "week8 api: bind() failed\n");
        close(fd);
        return -1;
    }

    if (listen(fd, 64) != 0) {
        fprintf(server->err, "week8 api: listen() failed\n");
        close(fd);
        return -1;
    }

    struct sockaddr_in actual;
    socklen_t actual_len = sizeof(actual);
    if (getsockname(fd, (struct sockaddr *)&actual, &actual_len) == 0) {
        server->actual_port = ntohs(actual.sin_port);
    }

    server->listen_fd = fd;
    server->stop_requested = 0;
    if (server->dispatch_mode == WEEK8_DISPATCH_POOL) {
        if (init_worker_pool(server) != 0) {
            fprintf(server->err, "week8 api: worker pool init failed\n");
            close(fd);
            server->listen_fd = -1;
            return -1;
        }
        server->pool_inited = 1;
    }
    return 0;
}

int week8_api_server_serve(Week8ApiServer *server) {
    if (!server || server->listen_fd < 0) return -1;
    if (server->dispatch_mode == WEEK8_DISPATCH_PER_REQUEST) {
        return serve_per_request_mode(server);
    }

    while (!server->stop_requested) {
        int client_fd = accept(server->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (server->stop_requested) break;
            if (errno == EINTR) continue;
            fprintf(server->err, "week8 api: accept() failed\n");
            return -1;
        }
        RequestContext *ctx = (RequestContext *)calloc(1, sizeof(RequestContext));
        if (!ctx) {
            send_json_response(client_fd,
                               500,
                               "Internal Server Error",
                               "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"out of memory\",\"retryable\":false}}");
            close(client_fd);
            continue;
        }
        pthread_mutex_lock(&server->queue_mu);
        int q_size_snapshot = server->queue_size;
        pthread_mutex_unlock(&server->queue_mu);
        ctx->client_fd = client_fd;
        if (server->timeout_policy == WEEK8_TIMEOUT_FIXED) {
            ctx->timeout_ms = server->fixed_timeout_ms;
        } else {
            ctx->timeout_ms = compute_dynamic_timeout_ms(q_size_snapshot, server->queue_capacity);
        }
        ctx->deadline = add_ms(now_realtime(), ctx->timeout_ms);
        atomic_store(&ctx->cancel_token, 0);
        registry_add(server, ctx);

        if (queue_push(server, ctx) != 0) {
            registry_remove(server, ctx);
            send_json_response(
                client_fd,
                503,
                "Service Unavailable",
                "{\"ok\":false,\"error\":{\"code\":\"QUEUE_FULL\",\"message\":\"server busy\",\"retryable\":true}}");
            close(client_fd);
            free(ctx);
        }
    }
    return 0;
}

void week8_api_server_request_stop(Week8ApiServer *server) {
    if (!server) return;
    server->stop_requested = 1;
    if (server->pool_inited) {
        pthread_cond_broadcast(&server->queue_not_empty);
        pthread_cond_broadcast(&server->registry_cv);
    }
    if (server->listen_fd >= 0) {
        (void)shutdown(server->listen_fd, SHUT_RDWR);
        close(server->listen_fd);
        server->listen_fd = -1;
    }
}

void week8_api_server_destroy(Week8ApiServer *server) {
    if (!server) return;
    destroy_worker_pool(server);
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
}

uint16_t week8_api_server_port(const Week8ApiServer *server) {
    if (!server) return 0;
    return server->actual_port;
}
