#include "csv_storage.h"
#include "sql_processor.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int idx;
    int rc;
} InsertThreadArg;

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

static void *insert_thread_main(void *arg) {
    InsertThreadArg *a = (InsertThreadArg *)arg;
    char sql[256];
    snprintf(sql,
             sizeof(sql),
             "INSERT INTO week8_lock_users VALUES (%d, 'user_%d', 'u%d@example.com');",
             1000 + a->idx,
             a->idx,
             a->idx);
    a->rc = sql_processor_run_text(sql, stdout, stderr);
    return NULL;
}

int main(void) {
    remove("data/week8_lock_users.csv");
    if (write_file("data/week8_lock_users.csv", "id,name,email\n") != 0) {
        return fail("seed lock table failed");
    }

    enum { kThreads = 16 };
    pthread_t threads[kThreads];
    InsertThreadArg args[kThreads];

    for (int i = 0; i < kThreads; ++i) {
        args[i].idx = i;
        args[i].rc = -1;
        if (pthread_create(&threads[i], NULL, insert_thread_main, &args[i]) != 0) {
            return fail("pthread_create failed");
        }
    }

    for (int i = 0; i < kThreads; ++i) {
        pthread_join(threads[i], NULL);
        if (args[i].rc != 0) {
            return fail("thread insert failed");
        }
    }

    size_t row_count = 0;
    if (csv_storage_data_row_count("week8_lock_users", &row_count) != 0) {
        return fail("count rows failed");
    }
    if (row_count != (size_t)kThreads) {
        fprintf(stderr, "row count mismatch: got %zu want %d\n", row_count, kThreads);
        return 1;
    }

    remove("data/week8_lock_users.csv");
    return 0;
}
