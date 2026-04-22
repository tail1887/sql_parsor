#include "week8/api_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static Week8ApiServer *g_server = NULL;

static void on_signal(int signo) {
    (void)signo;
    if (g_server) week8_api_server_request_stop(g_server);
}

int main(void) {
    Week8ApiServer server;
    Week8ApiServerConfig cfg = {.host = "127.0.0.1", .port = 8080};

    if (week8_api_server_init(&server, &cfg, stderr) != 0) {
        return EXIT_FAILURE;
    }

    g_server = &server;
    (void)signal(SIGINT, on_signal);
    (void)signal(SIGTERM, on_signal);

    fprintf(stdout, "week8_api_server listening on 127.0.0.1:%u\n", week8_api_server_port(&server));
    fflush(stdout);

    int rc = week8_api_server_serve(&server);
    week8_api_server_destroy(&server);
    g_server = NULL;
    return (rc == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
