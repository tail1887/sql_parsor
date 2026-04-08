#include "sql_processor.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(void) { fprintf(stderr, "usage: sql_processor <path.sql>\n"); }

int main(int argc, char **argv) {
    if (argc != 2) {
        usage();
        return 1;
    }

    (void)argv;
    fprintf(stderr,
            "sql_processor: not implemented (see docs/ and AGENTS.md for build order)\n");
    return 3;
}
