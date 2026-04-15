#include "week7/bplus_tree.h"

#include <stdio.h>
#include <stdlib.h>

static int fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void) {
    BPlusTree *t = bplus_create();
    if (!t) {
        return fail("create");
    }

    for (int64_t i = 1; i <= 50; i++) {
        if (bplus_insert(t, i, (size_t)(100 + i)) != 0) {
            bplus_destroy(t);
            return fail("insert sequential");
        }
    }

    for (int64_t i = 1; i <= 50; i++) {
        size_t p = 0;
        if (bplus_search(t, i, &p) != 0 || p != (size_t)(100 + i)) {
            bplus_destroy(t);
            return fail("search");
        }
    }

    if (bplus_search(t, 99, &(size_t){0}) == 0) {
        bplus_destroy(t);
        return fail("search miss");
    }

    if (bplus_insert(t, 25, 0) != -1) {
        bplus_destroy(t);
        return fail("dup");
    }

    bplus_destroy(t);
    return 0;
}
