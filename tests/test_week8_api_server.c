#include "week8/api_server.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    Week8ApiServer *server;
    int serve_rc;
} ServeCtx;

typedef struct {
    uint16_t port;
    int rc;
} ClientCtx;

static int fail(const char *m) {
    fprintf(stderr, "%s\n", m);
    return 1;
}

static int write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    if (fwrite(content, 1, strlen(content), fp) != strlen(content)) {
        fclose(fp);
        return -1;
    }
    return fclose(fp);
}

static void *serve_thread(void *arg) {
    ServeCtx *ctx = (ServeCtx *)arg;
    ctx->serve_rc = week8_api_server_serve(ctx->server);
    return NULL;
}

static int http_request(uint16_t port, const char *request, char *resp, size_t resp_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    size_t req_len = strlen(request);
    if (send(fd, request, req_len, 0) != (ssize_t)req_len) {
        close(fd);
        return -1;
    }

    size_t total = 0;
    while (total + 1 < resp_cap) {
        ssize_t n = recv(fd, resp + total, resp_cap - 1 - total, 0);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) break;
        total += (size_t)n;
    }
    close(fd);
    if (total == 0) return -1;
    resp[total] = '\0';
    return 0;
}

static void *health_client_thread(void *arg) {
    ClientCtx *ctx = (ClientCtx *)arg;
    char resp[1024];
    if (http_request(ctx->port,
                     "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
                     resp,
                     sizeof(resp)) != 0) {
        ctx->rc = -1;
        return NULL;
    }
    if (strstr(resp, "HTTP/1.1 200 OK") == NULL || strstr(resp, "\"status\":\"ok\"") == NULL) {
        ctx->rc = -1;
        return NULL;
    }
    ctx->rc = 0;
    return NULL;
}

int main(void) {
    remove("data/week8_api_users.csv");
    if (write_file("data/week8_api_users.csv", "id,name,email\n1,alice,alice@example.com\n") != 0) {
        return fail("seed api table failed");
    }

    Week8ApiServer server;
    Week8ApiServerConfig cfg = {.host = "127.0.0.1", .port = 0, .worker_count = 2, .queue_capacity = 4};
    if (week8_api_server_init(&server, &cfg, stderr) != 0) return fail("init failed");

    uint16_t port = week8_api_server_port(&server);
    if (port == 0) {
        week8_api_server_destroy(&server);
        return fail("ephemeral port not assigned");
    }

    ServeCtx ctx = {.server = &server, .serve_rc = -1};
    pthread_t th;
    if (pthread_create(&th, NULL, serve_thread, &ctx) != 0) {
        week8_api_server_destroy(&server);
        return fail("pthread_create failed");
    }

    char resp[4096];
    if (http_request(port,
                     "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
                     resp,
                     sizeof(resp)) != 0) {
        week8_api_server_request_stop(&server);
        pthread_join(th, NULL);
        week8_api_server_destroy(&server);
        return fail("health request failed");
    }
    if (strstr(resp, "HTTP/1.1 200 OK") == NULL || strstr(resp, "\"status\":\"ok\"") == NULL) {
        week8_api_server_request_stop(&server);
        pthread_join(th, NULL);
        week8_api_server_destroy(&server);
        return fail("health response mismatch");
    }

    if (http_request(port,
                     "GET /unknown HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
                     resp,
                     sizeof(resp)) != 0) {
        week8_api_server_request_stop(&server);
        pthread_join(th, NULL);
        week8_api_server_destroy(&server);
        return fail("unknown route request failed");
    }
    if (strstr(resp, "HTTP/1.1 404 Not Found") == NULL || strstr(resp, "\"code\":\"NOT_FOUND\"") == NULL) {
        week8_api_server_request_stop(&server);
        pthread_join(th, NULL);
        week8_api_server_destroy(&server);
        return fail("unknown route response mismatch");
    }

    if (http_request(port,
                     "POST /query HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: 40\r\n\r\n{\"sql\":\"SELECT * FROM week8_api_users;\"}",
                     resp,
                     sizeof(resp)) != 0) {
        week8_api_server_request_stop(&server);
        pthread_join(th, NULL);
        week8_api_server_destroy(&server);
        return fail("query request failed");
    }
    if (strstr(resp, "HTTP/1.1 200 OK") == NULL || strstr(resp, "\"ok\":true") == NULL ||
        strstr(resp, "id\\tname\\temail") == NULL) {
        week8_api_server_request_stop(&server);
        pthread_join(th, NULL);
        week8_api_server_destroy(&server);
        return fail("query response mismatch");
    }

    if (http_request(port,
                     "POST /query HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: 2\r\n\r\n{}",
                     resp,
                     sizeof(resp)) != 0) {
        week8_api_server_request_stop(&server);
        pthread_join(th, NULL);
        week8_api_server_destroy(&server);
        return fail("invalid query request failed");
    }
    if (strstr(resp, "HTTP/1.1 400 Bad Request") == NULL || strstr(resp, "\"code\":\"INVALID_REQUEST\"") == NULL) {
        week8_api_server_request_stop(&server);
        pthread_join(th, NULL);
        week8_api_server_destroy(&server);
        return fail("invalid query response mismatch");
    }

    const int kClientCount = 8;
    pthread_t clients[kClientCount];
    ClientCtx client_ctx[kClientCount];
    for (int i = 0; i < kClientCount; ++i) {
        client_ctx[i].port = port;
        client_ctx[i].rc = -1;
        if (pthread_create(&clients[i], NULL, health_client_thread, &client_ctx[i]) != 0) {
            week8_api_server_request_stop(&server);
            pthread_join(th, NULL);
            week8_api_server_destroy(&server);
            return fail("health client thread create failed");
        }
    }
    for (int i = 0; i < kClientCount; ++i) {
        pthread_join(clients[i], NULL);
        if (client_ctx[i].rc != 0) {
            week8_api_server_request_stop(&server);
            pthread_join(th, NULL);
            week8_api_server_destroy(&server);
            return fail("parallel health request failed");
        }
    }

    week8_api_server_request_stop(&server);
    pthread_join(th, NULL);
    week8_api_server_destroy(&server);

    if (ctx.serve_rc != 0) return fail("serve loop exit mismatch");
    remove("data/week8_api_users.csv");
    return 0;
}
