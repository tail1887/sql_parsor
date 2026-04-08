#include "csv_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *m) {
    fprintf(stderr, "%s\n", m);
    return 1;
}

static int test_read_users(void) {
    CsvTable *t = NULL;
    if (csv_storage_read_table("users", &t) != 0 || !t) {
        return fail("read users");
    }
    if (t->header_count != 3) {
        csv_storage_free_table(t);
        return fail("header count");
    }
    if (strcmp(t->headers[0], "id") != 0 || strcmp(t->headers[1], "name") != 0) {
        csv_storage_free_table(t);
        return fail("header values");
    }
    csv_storage_free_table(t);
    return 0;
}

static int write_seed_table(void) {
    FILE *fp = fopen("data/test_csv_storage.csv", "wb");
    if (!fp) return -1;
    fputs("id,name,note\n", fp);
    fputs("1,alice,ok", fp);
    return fclose(fp);
}

static int remove_seed_table(void) {
    remove("data/test_csv_storage.csv");
    return 0;
}

static int test_append_and_read(void) {
    if (write_seed_table() != 0) {
        return fail("seed table create");
    }

    SqlValue vals[3];
    vals[0].kind = SQL_VALUE_INT;
    vals[0].text = "2";
    vals[1].kind = SQL_VALUE_STRING;
    vals[1].text = "bob, \"junior\"";
    vals[2].kind = SQL_VALUE_NULL;
    vals[2].text = NULL;

    if (csv_storage_append_insert_row("test_csv_storage", vals, 3) != 0) {
        remove_seed_table();
        return fail("append row");
    }

    CsvTable *t = NULL;
    if (csv_storage_read_table("test_csv_storage", &t) != 0 || !t) {
        remove_seed_table();
        return fail("read appended");
    }
    if (t->row_count != 2) {
        csv_storage_free_table(t);
        remove_seed_table();
        return fail("row count");
    }

    if (strcmp(t->rows[1][0], "2") != 0) {
        csv_storage_free_table(t);
        remove_seed_table();
        return fail("id value");
    }
    if (strcmp(t->rows[1][1], "bob, \"junior\"") != 0) {
        csv_storage_free_table(t);
        remove_seed_table();
        return fail("quoted value");
    }
    if (strcmp(t->rows[1][2], "") != 0) {
        csv_storage_free_table(t);
        remove_seed_table();
        return fail("null empty field");
    }

    csv_storage_free_table(t);
    remove_seed_table();
    return 0;
}

int main(void) {
    if (test_read_users() != 0) return 1;
    if (test_append_and_read() != 0) return 1;
    return 0;
}
