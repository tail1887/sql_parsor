#ifndef WEEK8_API_SERVER_H
#define WEEK8_API_SERVER_H

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const char *host; /* NULL or empty -> 127.0.0.1 */
    uint16_t port;    /* 0 allowed: OS picks ephemeral port */
    int worker_count; /* <=0 -> default */
    int queue_capacity; /* <=0 -> default */
} Week8ApiServerConfig;

typedef enum {
    WEEK8_DISPATCH_POOL = 0,
    WEEK8_DISPATCH_PER_REQUEST = 1
} Week8DispatchMode;

typedef struct Week8ApiServer {
    int listen_fd;
    uint16_t actual_port;
    volatile sig_atomic_t stop_requested;
    FILE *err;
    Week8DispatchMode dispatch_mode;
    int worker_count;
    int queue_capacity;
    void **client_queue;
    int queue_head;
    int queue_tail;
    int queue_size;
    pthread_mutex_t queue_mu;
    pthread_cond_t queue_not_empty;
    pthread_cond_t queue_not_full;
    pthread_mutex_t registry_mu;
    pthread_cond_t registry_cv;
    void *active_requests;
    pthread_t *workers;
    pthread_t watcher;
    int pool_inited;
} Week8ApiServer;

int week8_api_server_init(Week8ApiServer *server, const Week8ApiServerConfig *config, FILE *err);
int week8_api_server_serve(Week8ApiServer *server);
void week8_api_server_request_stop(Week8ApiServer *server);
void week8_api_server_destroy(Week8ApiServer *server);
uint16_t week8_api_server_port(const Week8ApiServer *server);

#endif
