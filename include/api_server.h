#ifndef API_SERVER_H
#define API_SERVER_H

#include <stddef.h>
#include <stdint.h>

typedef struct ApiServer ApiServer;

typedef struct {
    uint16_t port;
    size_t worker_count;
    size_t queue_capacity;
} ApiServerConfig;

int api_server_start(ApiServer **out_server, const ApiServerConfig *config);
void api_server_stop(ApiServer *server);
void api_server_destroy(ApiServer *server);
uint16_t api_server_port(const ApiServer *server);

#endif
