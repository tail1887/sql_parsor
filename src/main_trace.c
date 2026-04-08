#include "sql_processor.h"

#include <stdio.h>

static void usage(void) { fprintf(stderr, "usage: sql_processor_trace <path.sql> <trace.jsonl>\n"); }

int main(int argc, char **argv) {
    FILE *trace = NULL;
    int rc;

    if (argc != 3) {
        usage();
        return 1;
    }

    trace = fopen(argv[2], "wb");
    if (!trace) {
        fprintf(stderr, "io error: failed to open trace output %s\n", argv[2]);
        return 3;
    }

    rc = sql_processor_run_file_trace(argv[1], stdout, stderr, trace);
    fclose(trace);
    return rc;
}
