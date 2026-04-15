/* WEEK7: B+ 트리만 대량 삽입·조회 타이밍 (CSV/SQL 제외). docs/weeks/WEEK7/assignment.md */
#include "week7/bplus_tree.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int run_full(long long n) {
    BPlusTree *t = bplus_create();
    if (!t) {
        return 1;
    }

    clock_t c0 = clock();
    for (long long i = 1; i <= n; i++) {
        if (bplus_insert(t, i, (size_t)i) != 0) {
            fprintf(stderr, "insert fail at %lld\n", i);
            bplus_destroy(t);
            return 1;
        }
    }
    clock_t c1 = clock();

    size_t junk = 0;
    clock_t c2 = clock();
    for (long long i = 1; i <= n; i++) {
        if (bplus_search(t, i, &junk) != 0 || junk != (size_t)i) {
            fprintf(stderr, "search fail at %lld\n", i);
            bplus_destroy(t);
            return 1;
        }
    }
    clock_t c3 = clock();

    bplus_destroy(t);

    double ins = (double)(c1 - c0) / (double)CLOCKS_PER_SEC;
    double srch = (double)(c3 - c2) / (double)CLOCKS_PER_SEC;
    printf("n=%lld insert_sec=%.3f search_sec=%.3f\n", n, ins, srch);
    return 0;
}

/* Deterministic PRNG (LCG) for reproducible bench across machines */
static uint64_t prng(uint64_t *s) {
    *s = *s * 6364136223846793005ULL + 1ULL;
    return *s;
}

static int run_compare(long long n, long long k) {
    if (k < 1 || k > n) {
        fprintf(stderr, "compare: need 1 <= k <= n\n");
        return 1;
    }

    BPlusTree *t = bplus_create();
    if (!t) {
        return 1;
    }

    clock_t c0 = clock();
    for (long long i = 1; i <= n; i++) {
        if (bplus_insert(t, i, (size_t)i) != 0) {
            fprintf(stderr, "insert fail at %lld\n", i);
            bplus_destroy(t);
            return 1;
        }
    }
    clock_t c1 = clock();

    int64_t *row_id = (int64_t *)malloc((size_t)n * sizeof(int64_t));
    if (!row_id) {
        bplus_destroy(t);
        return 1;
    }
    for (long long i = 0; i < n; i++) {
        row_id[i] = i + 1;
    }

    int64_t *queries = (int64_t *)malloc((size_t)k * sizeof(int64_t));
    if (!queries) {
        free(row_id);
        bplus_destroy(t);
        return 1;
    }
    uint64_t seed = 0xC0FFEEULL;
    for (long long i = 0; i < k; i++) {
        queries[i] = 1 + (int64_t)(prng(&seed) % (uint64_t)n);
    }

    size_t junk = 0;
    clock_t c2 = clock();
    for (long long i = 0; i < k; i++) {
        if (bplus_search(t, queries[i], &junk) != 0 || junk != (size_t)queries[i]) {
            fprintf(stderr, "bplus_search fail q=%lld\n", (long long)queries[i]);
            free(queries);
            free(row_id);
            bplus_destroy(t);
            return 1;
        }
    }
    clock_t c3 = clock();

    clock_t c4 = clock();
    for (long long i = 0; i < k; i++) {
        int64_t q = queries[i];
        long long j;
        for (j = 0; j < n; j++) {
            if (row_id[j] == q) {
                break;
            }
        }
        if (j == n) {
            fprintf(stderr, "linear miss q=%lld\n", (long long)q);
            free(queries);
            free(row_id);
            bplus_destroy(t);
            return 1;
        }
    }
    clock_t c5 = clock();

    bplus_destroy(t);
    free(queries);
    free(row_id);

    double ins = (double)(c1 - c0) / (double)CLOCKS_PER_SEC;
    double ix = (double)(c3 - c2) / (double)CLOCKS_PER_SEC;
    double lin = (double)(c5 - c4) / (double)CLOCKS_PER_SEC;
    printf("mode=compare n=%lld k=%lld insert_sec=%.3f index_lookup_sec=%.3f "
           "linear_scan_sec=%.3f speedup_linear_over_index=%.2fx\n",
           n, k, ins, ix, lin, lin > 0 ? lin / ix : 0.0);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "compare") == 0) {
        if (argc != 4) {
            fprintf(stderr, "usage: bench_bplus compare <n> <k>\n");
            return 1;
        }
        long long n = strtoll(argv[2], NULL, 10);
        long long k = strtoll(argv[3], NULL, 10);
        if (n < 1 || n > 50000000LL || k < 1) {
            fprintf(stderr, "usage: bench_bplus compare <n> <k>\n");
            return 1;
        }
        return run_compare(n, k);
    }

    long long n = 1000000;
    if (argc > 1) {
        n = strtoll(argv[1], NULL, 10);
    }
    if (n < 1 || n > 50000000) {
        fprintf(stderr, "usage: bench_bplus [n]\n       bench_bplus compare <n> <k>\n");
        return 1;
    }

    return run_full(n);
}
