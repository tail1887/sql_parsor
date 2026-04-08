#include "sql_processor.h"

#include <stdio.h>

static void usage(void) { fprintf(stderr, "usage: sql_processor <path.sql>\n"); }

int main(int argc, char **argv) {
    if (argc != 2) {
        usage();
        return 1;
    }

    return sql_processor_run_file(argv[1], stdout, stderr);
}
