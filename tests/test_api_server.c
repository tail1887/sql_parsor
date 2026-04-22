#include "api_server.h"
#include "week7/week7_index.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    uint16_t port;
    char name[32];
    char email[64];
    int ok;
} InsertThreadArgs;

static void *insert_thread_main(void *arg);

static int fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

static int write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "wb");
    size_t want = strlen(content);
    if (!fp) {
        return -1;
    }
    if (fwrite(content, 1, want, fp) != want) {
        fclose(fp);
        return -1;
    }
    return fclose(fp);
}

static void cleanup_files(void) {
    week7_reset();
    remove("data/test_api_server_users.csv");
}

static int seed_table(void) {
    week7_reset();
    return write_file("data/test_api_server_users.csv", "id,name,email\n1,alice,alice@example.com\n");
}

static int open_client(uint16_t port) {
    int fd = -1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int read_all(int fd, char **out_response) {
    char *buffer = NULL;
    size_t used = 0;
    size_t cap = 4096;

    buffer = calloc(cap, 1);
    if (!buffer) {
        return -1;
    }

    for (;;) {
        ssize_t n = recv(fd, buffer + used, cap - used - 1, 0);
        if (n < 0) {
            free(buffer);
            return -1;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
        buffer[used] = '\0';
        if (used + 1 >= cap) {
            char *next = realloc(buffer, cap * 2);
            if (!next) {
                free(buffer);
                return -1;
            }
            memset(next + cap, 0, cap);
            buffer = next;
            cap *= 2;
        }
    }

    *out_response = buffer;
    return 0;
}

static int send_http_request(uint16_t port, const char *request, char **out_response) {
    int fd = open_client(port);
    if (fd < 0) {
        return -1;
    }
    if (send_all(fd, request, strlen(request)) != 0) {
        close(fd);
        return -1;
    }
    shutdown(fd, SHUT_WR);
    if (read_all(fd, out_response) != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int build_request(char *buffer, size_t cap, const char *method, const char *path, const char *body,
                         int include_length) {
    if (include_length) {
        return snprintf(buffer, cap,
                        "%s %s HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Content-Length: %zu\r\n"
                        "\r\n"
                        "%s",
                        method, path, strlen(body), body);
    }
    return snprintf(buffer, cap,
                    "%s %s HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "\r\n"
                    "%s",
                    method, path, body);
}

static int start_server(ApiServer **out_server, uint16_t *out_port, size_t workers, size_t queue_capacity) {
    ApiServerConfig config = {0};

    config.port = 0;
    config.worker_count = workers;
    config.queue_capacity = queue_capacity;
    if (api_server_start(out_server, &config) != 0 || !*out_server) {
        return -1;
    }
    *out_port = api_server_port(*out_server);
    return 0;
}

static int test_basic_requests(void) {
    ApiServer *server = NULL;
    uint16_t port = 0;
    char request[1024];
    char *response = NULL;
    pthread_t threads[4];
    InsertThreadArgs args[4];

    if (cleanup_files(), seed_table() != 0) {
        return fail("seed basic");
    }
    if (start_server(&server, &port, 4, 16) != 0) {
        cleanup_files();
        return fail("start basic server");
    }

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT id, email FROM test_api_server_users;\"}", 1);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("select request");
    }
    if (strstr(response, "HTTP/1.1 200 OK") == NULL || strstr(response, "\"statementType\":\"select\"") == NULL ||
        strstr(response, "\"message\":\"1 row selected\"") == NULL ||
        strstr(response, "\"columns\":[\"id\",\"email\"]") == NULL ||
        strstr(response, "alice@example.com") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        return fail("select response");
    }
    free(response);
    response = NULL;

    build_request(request, sizeof request, "GET", "/query", "", 0);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("get request");
    }
    if (strstr(response, "HTTP/1.1 405 Method Not Allowed") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        return fail("get status");
    }
    free(response);
    response = NULL;

    build_request(request, sizeof request, "POST", "/missing", "{\"sql\":\"SELECT * FROM test_api_server_users;\"}",
                  1);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("missing request");
    }
    if (strstr(response, "HTTP/1.1 404 Not Found") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        return fail("missing status");
    }
    free(response);
    response = NULL;

    build_request(request, sizeof request, "POST", "/query", "{\"sql\":123}", 1);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("invalid json request");
    }
    if (strstr(response, "HTTP/1.1 400 Bad Request") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        return fail("invalid json status");
    }
    free(response);
    response = NULL;

    for (size_t i = 0; i < 4; i++) {
        snprintf(args[i].name, sizeof args[i].name, "user%zu", i);
        snprintf(args[i].email, sizeof args[i].email, "user%zu@example.com", i);
        args[i].port = port;
        args[i].ok = 0;
        if (pthread_create(&threads[i], NULL, insert_thread_main, &args[i]) != 0) {
            api_server_destroy(server);
            cleanup_files();
            return fail("pthread create");
        }
    }
    for (size_t i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
        if (!args[i].ok) {
            api_server_destroy(server);
            cleanup_files();
            return fail("concurrent insert failed");
        }
    }

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT * FROM test_api_server_users;\"}", 1);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("post-concurrency select");
    }
    if (strstr(response, "\"message\":\"5 rows selected\"") == NULL || strstr(response, "user0") == NULL ||
        strstr(response, "user1") == NULL || strstr(response, "user2") == NULL ||
        strstr(response, "user3") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        return fail("concurrency response");
    }
    free(response);
    response = NULL;

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"INSERT INTO test_api_server_users VALUES ('bob', 'bob@example.com');\"}", 1);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("single insert request");
    }
    if (strstr(response, "\"statementType\":\"insert\"") == NULL ||
        strstr(response, "\"affectedRows\":1") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        return fail("single insert response");
    }
    free(response);
    response = NULL;

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT * FROM test_api_server_users;\"}", 1);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("final select request");
    }
    if (strstr(response, "\"message\":\"6 rows selected\"") == NULL || strstr(response, "bob@example.com") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        return fail("final select response");
    }

    free(response);
    api_server_destroy(server);
    cleanup_files();
    return 0;
}

static void *insert_thread_main(void *arg) {
    InsertThreadArgs *thread_arg = (InsertThreadArgs *)arg;
    char request[1024];
    char body[256];
    char *response = NULL;

    snprintf(body, sizeof body, "{\"sql\":\"INSERT INTO test_api_server_users VALUES ('%s', '%s');\"}",
             thread_arg->name, thread_arg->email);
    build_request(request, sizeof request, "POST", "/query", body, 1);
    if (send_http_request(thread_arg->port, request, &response) == 0 &&
        strstr(response, "\"statementType\":\"insert\"") != NULL) {
        thread_arg->ok = 1;
    }
    free(response);
    return NULL;
}

static int test_queue_full(void) {
    ApiServer *server = NULL;
    uint16_t port = 0;
    int client1 = -1;
    int client2 = -1;
    int client3 = -1;
    char request[1024];
    char *response = NULL;

    if (cleanup_files(), seed_table() != 0) {
        return fail("seed queue");
    }
    if (start_server(&server, &port, 1, 1) != 0) {
        cleanup_files();
        return fail("start queue server");
    }

    client1 = open_client(port);
    client2 = open_client(port);
    client3 = open_client(port);
    if (client1 < 0 || client2 < 0 || client3 < 0) {
        api_server_destroy(server);
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("open clients");
    }

    snprintf(request, sizeof request,
             "POST /query HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Length: 64\r\n"
             "\r\n"
             "{\"sql\":\"SELECT");
    if (send_all(client1, request, strlen(request)) != 0) {
        api_server_destroy(server);
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("send client1");
    }
    usleep(200000);

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT * FROM test_api_server_users;\"}", 1);
    if (send_all(client2, request, strlen(request)) != 0) {
        api_server_destroy(server);
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("send client2");
    }
    shutdown(client2, SHUT_WR);
    usleep(200000);

    if (send_all(client3, request, strlen(request)) != 0) {
        api_server_destroy(server);
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("send client3");
    }
    shutdown(client3, SHUT_WR);
    if (read_all(client3, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("read client3");
    }
    if (strstr(response, "HTTP/1.1 503 Service Unavailable") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("queue full status");
    }
    free(response);
    response = NULL;

    shutdown(client1, SHUT_RDWR);
    close(client1);
    client1 = -1;

    if (read_all(client2, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        close(client2);
        close(client3);
        return fail("read client2");
    }
    if (strstr(response, "HTTP/1.1 200 OK") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        close(client2);
        close(client3);
        return fail("queued request response");
    }

    free(response);
    close(client2);
    close(client3);
    api_server_destroy(server);
    cleanup_files();
    return 0;
}

int main(void) {
    if (test_basic_requests() != 0) {
        return 1;
    }
    if (test_queue_full() != 0) {
        return 1;
    }
    return 0;
}
