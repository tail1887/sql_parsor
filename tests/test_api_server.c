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

typedef struct {
    uint16_t port;
    int ok;
} SelectThreadArgs;

typedef struct {
    uint16_t port;
    size_t iterations;
    int ok;
} RepeatedSelectThreadArgs;

static void *insert_thread_main(void *arg);
static void *select_thread_main(void *arg);
static void *repeated_select_thread_main(void *arg);
static int test_slow_client_does_not_block_execution(void);

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
    InsertThreadArgs insert_args[4];
    SelectThreadArgs select_args[4];

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
        select_args[i].port = port;
        select_args[i].ok = 0;
        if (pthread_create(&threads[i], NULL, select_thread_main, &select_args[i]) != 0) {
            api_server_destroy(server);
            cleanup_files();
            return fail("pthread create select");
        }
    }
    for (size_t i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
        if (!select_args[i].ok) {
            api_server_destroy(server);
            cleanup_files();
            return fail("concurrent select failed");
        }
    }

    for (size_t i = 0; i < 4; i++) {
        snprintf(insert_args[i].name, sizeof insert_args[i].name, "user%zu", i);
        snprintf(insert_args[i].email, sizeof insert_args[i].email, "user%zu@example.com", i);
        insert_args[i].port = port;
        insert_args[i].ok = 0;
        if (pthread_create(&threads[i], NULL, insert_thread_main, &insert_args[i]) != 0) {
            api_server_destroy(server);
            cleanup_files();
            return fail("pthread create");
        }
    }
    for (size_t i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
        if (!insert_args[i].ok) {
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

static int test_mixed_read_write_requests(void) {
    ApiServer *server = NULL;
    uint16_t port = 0;
    char request[1024];
    char *response = NULL;
    pthread_t read_threads[2];
    pthread_t write_threads[4];
    RepeatedSelectThreadArgs read_args[2];
    InsertThreadArgs write_args[4];

    if (cleanup_files(), seed_table() != 0) {
        return fail("seed mixed");
    }
    if (start_server(&server, &port, 6, 32) != 0) {
        cleanup_files();
        return fail("start mixed server");
    }

    for (size_t i = 0; i < 2; i++) {
        read_args[i].port = port;
        read_args[i].iterations = 24;
        read_args[i].ok = 0;
        if (pthread_create(&read_threads[i], NULL, repeated_select_thread_main, &read_args[i]) != 0) {
            api_server_destroy(server);
            cleanup_files();
            return fail("pthread create mixed read");
        }
    }

    usleep(50000);

    for (size_t i = 0; i < 4; i++) {
        snprintf(write_args[i].name, sizeof write_args[i].name, "mixuser%zu", i);
        snprintf(write_args[i].email, sizeof write_args[i].email, "mixuser%zu@example.com", i);
        write_args[i].port = port;
        write_args[i].ok = 0;
        if (pthread_create(&write_threads[i], NULL, insert_thread_main, &write_args[i]) != 0) {
            api_server_destroy(server);
            cleanup_files();
            return fail("pthread create mixed write");
        }
    }

    for (size_t i = 0; i < 4; i++) {
        pthread_join(write_threads[i], NULL);
        if (!write_args[i].ok) {
            api_server_destroy(server);
            cleanup_files();
            return fail("mixed write failed");
        }
    }

    for (size_t i = 0; i < 2; i++) {
        pthread_join(read_threads[i], NULL);
        if (!read_args[i].ok) {
            api_server_destroy(server);
            cleanup_files();
            return fail("mixed read failed");
        }
    }

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT * FROM test_api_server_users;\"}", 1);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("mixed final select request");
    }
    if (strstr(response, "\"message\":\"5 rows selected\"") == NULL || strstr(response, "mixuser0") == NULL ||
        strstr(response, "mixuser1") == NULL || strstr(response, "mixuser2") == NULL ||
        strstr(response, "mixuser3") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        return fail("mixed final select response");
    }

    free(response);
    api_server_destroy(server);
    cleanup_files();
    return 0;
}

static int test_slow_client_does_not_block_execution(void) {
    ApiServer *server = NULL;
    uint16_t port = 0;
    int slow_client = -1;
    char request[1024];
    char *response = NULL;

    if (cleanup_files(), seed_table() != 0) {
        return fail("seed slow client");
    }
    if (start_server(&server, &port, 2, 4) != 0) {
        cleanup_files();
        return fail("start slow client server");
    }

    slow_client = open_client(port);
    if (slow_client < 0) {
        api_server_destroy(server);
        cleanup_files();
        return fail("open slow client");
    }

    snprintf(request, sizeof request,
             "POST /query HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Length: 64\r\n"
             "\r\n"
             "{\"sql\":\"SELECT");
    if (send_all(slow_client, request, strlen(request)) != 0) {
        api_server_destroy(server);
        cleanup_files();
        close(slow_client);
        return fail("send slow client");
    }

    usleep(100000);

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT * FROM test_api_server_users;\"}", 1);
    if (send_http_request(port, request, &response) != 0) {
        api_server_destroy(server);
        cleanup_files();
        close(slow_client);
        return fail("fast request during slow client");
    }
    if (strstr(response, "HTTP/1.1 200 OK") == NULL || strstr(response, "\"statementType\":\"select\"") == NULL) {
        free(response);
        api_server_destroy(server);
        cleanup_files();
        close(slow_client);
        return fail("fast request blocked by slow client");
    }

    free(response);
    shutdown(slow_client, SHUT_RDWR);
    close(slow_client);
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

static void *select_thread_main(void *arg) {
    SelectThreadArgs *thread_arg = (SelectThreadArgs *)arg;
    char request[1024];
    char *response = NULL;

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT email FROM test_api_server_users WHERE id = 1;\"}", 1);
    if (send_http_request(thread_arg->port, request, &response) == 0 &&
        strstr(response, "\"statementType\":\"select\"") != NULL &&
        strstr(response, "\"columns\":[\"email\"]") != NULL &&
        strstr(response, "alice@example.com") != NULL) {
        thread_arg->ok = 1;
    }
    free(response);
    return NULL;
}

static void *repeated_select_thread_main(void *arg) {
    RepeatedSelectThreadArgs *thread_arg = (RepeatedSelectThreadArgs *)arg;
    char request[1024];
    char *response = NULL;

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT email FROM test_api_server_users WHERE id = 1;\"}", 1);

    for (size_t i = 0; i < thread_arg->iterations; i++) {
        if (send_http_request(thread_arg->port, request, &response) != 0) {
            free(response);
            return NULL;
        }
        if (strstr(response, "\"statementType\":\"select\"") == NULL ||
            strstr(response, "\"columns\":[\"email\"]") == NULL ||
            strstr(response, "alice@example.com") == NULL) {
            free(response);
            return NULL;
        }
        free(response);
        response = NULL;
        usleep(5000);
    }

    thread_arg->ok = 1;
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
    setenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS", "250", 1);
    if (start_server(&server, &port, 1, 1) != 0) {
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        return fail("start queue server");
    }

    client1 = open_client(port);
    client2 = open_client(port);
    client3 = open_client(port);
    if (client1 < 0 || client2 < 0 || client3 < 0) {
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("open clients");
    }

    build_request(request, sizeof request, "POST", "/query",
                  "{\"sql\":\"SELECT * FROM test_api_server_users;\"}", 1);
    if (send_all(client1, request, strlen(request)) != 0) {
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("send client1");
    }
    shutdown(client1, SHUT_WR);
    usleep(200000);

    if (send_all(client2, request, strlen(request)) != 0) {
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
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
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("send client3");
    }
    shutdown(client3, SHUT_WR);
    if (read_all(client3, &response) != 0) {
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("read client3");
    }
    if (strstr(response, "HTTP/1.1 503 Service Unavailable") == NULL) {
        free(response);
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("queue full status");
    }
    free(response);
    response = NULL;

    if (read_all(client2, &response) != 0) {
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("read client2");
    }
    if (strstr(response, "HTTP/1.1 200 OK") == NULL) {
        free(response);
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("queued request response");
    }

    free(response);
    response = NULL;
    if (read_all(client1, &response) != 0) {
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("read client1");
    }
    if (strstr(response, "HTTP/1.1 200 OK") == NULL) {
        free(response);
        api_server_destroy(server);
        unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
        cleanup_files();
        close(client1);
        close(client2);
        close(client3);
        return fail("running request response");
    }

    free(response);
    unsetenv("SQL_API_SERVER_TEST_EXEC_DELAY_MS");
    close(client1);
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
    if (test_mixed_read_write_requests() != 0) {
        return 1;
    }
    if (test_slow_client_does_not_block_execution() != 0) {
        return 1;
    }
    if (test_queue_full() != 0) {
        return 1;
    }
    return 0;
}
