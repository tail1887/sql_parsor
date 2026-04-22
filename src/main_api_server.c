#include "api_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t g_should_stop = 0;

static void on_signal(int signo) {
    (void)signo;
    g_should_stop = 1;
}

static int parse_number(const char *text, unsigned long *out) {
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (!text || text[0] == '\0' || !end || *end != '\0') {
        return -1;
    }
    *out = value;
    return 0;
}

static void usage(void) { fprintf(stderr, "usage: sql_api_server [port] [workers]\n"); }

int main(int argc, char **argv) {
    unsigned long port = 8080;
    unsigned long workers = 4;
    ApiServerConfig config = {0};
    ApiServer *server = NULL;

    if (argc > 3) {
        usage();
        return 1;
    }
    if (argc >= 2 && parse_number(argv[1], &port) != 0) {
        usage();
        return 1;
    }
    if (argc == 3 && parse_number(argv[2], &workers) != 0) {
        usage();
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    config.port = (uint16_t)port;
    config.worker_count = (size_t)workers;
    config.queue_capacity = 64;

    if (api_server_start(&server, &config) != 0 || !server) {
        fprintf(stderr, "failed to start sql_api_server\n");
        return 1;
    }

    fprintf(stdout, "sql_api_server listening on port %u with %zu workers\n", api_server_port(server),
            config.worker_count);
    fflush(stdout);

    while (!g_should_stop) {
        sleep(1);
    }

    api_server_destroy(server);
    return 0;
}
