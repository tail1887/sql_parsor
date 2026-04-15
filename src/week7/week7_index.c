/* WEEK7: primary-key index + auto id (see docs/weeks/WEEK7/) */
#include "week7/week7_index.h"

#include "csv_storage.h"
#include "week7/bplus_tree.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

typedef struct Ent {
    char *table;
    BPlusTree *tree;
    int64_t next_id;
    size_t ncol;
    int loaded;
    int id_pk;
} Ent;

static Ent *g_ents;
static size_t g_n;
static size_t g_cap;

void week7_reset(void) {
    for (size_t i = 0; i < g_n; i++) {
        free(g_ents[i].table);
        bplus_destroy(g_ents[i].tree);
    }
    free(g_ents);
    g_ents = NULL;
    g_n = 0;
    g_cap = 0;
}

static Ent *find_ent(const char *table) {
    for (size_t i = 0; i < g_n; i++) {
        if (strcmp(g_ents[i].table, table) == 0) {
            return &g_ents[i];
        }
    }
    return NULL;
}

static Ent *alloc_ent(const char *table) {
    Ent *e = find_ent(table);
    if (e) {
        return e;
    }
    if (g_n >= g_cap) {
        size_t ncap = g_cap ? g_cap * 2 : 4;
        Ent *nv = realloc(g_ents, ncap * sizeof *nv);
        if (!nv) {
            return NULL;
        }
        g_ents = nv;
        g_cap = ncap;
    }
    g_ents[g_n].table = strdup(table);
    if (!g_ents[g_n].table) {
        return NULL;
    }
    g_ents[g_n].tree = NULL;
    g_ents[g_n].next_id = 1;
    g_ents[g_n].ncol = 0;
    g_ents[g_n].loaded = 0;
    g_ents[g_n].id_pk = 0;
    return &g_ents[g_n++];
}

int week7_ensure_loaded(const char *table) {
    if (!table) {
        return -1;
    }
    Ent *e = alloc_ent(table);
    if (!e) {
        return -1;
    }
    if (e->loaded) {
        return 0;
    }

    CsvTable *t = NULL;
    if (csv_storage_read_table(table, &t) != 0 || !t) {
        return -1;
    }
    if (t->header_count == 0) {
        csv_storage_free_table(t);
        return -1;
    }

    e->ncol = t->header_count;
    e->id_pk = (ascii_strcasecmp(t->headers[0], "id") == 0);
    if (!e->id_pk) {
        e->tree = NULL;
        e->loaded = 1;
        csv_storage_free_table(t);
        return 0;
    }

    e->tree = bplus_create();
    if (!e->tree) {
        csv_storage_free_table(t);
        return -1;
    }

    int64_t mx = 0;
    for (size_t r = 0; r < t->row_count; r++) {
        int64_t id = strtoll(t->rows[r][0], NULL, 10);
        if (id > mx) {
            mx = id;
        }
        if (bplus_insert_or_replace(e->tree, id, r) != 0) {
            csv_storage_free_table(t);
            return -1;
        }
    }
    e->next_id = mx + 1;
    e->loaded = 1;
    csv_storage_free_table(t);
    return 0;
}

int week7_table_has_id_pk(const char *table) {
    Ent *e = find_ent(table);
    return (e && e->loaded && e->id_pk) ? 1 : 0;
}

static int int64_to_sqlvalue(int64_t v, SqlValue *dst) {
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", (long long)v);
    dst->kind = SQL_VALUE_INT;
    dst->text = strdup(buf);
    return dst->text ? 0 : -1;
}

static int dup_sqlvalue(const SqlValue *src, SqlValue *dst) {
    dst->kind = src->kind;
    if (src->kind == SQL_VALUE_NULL) {
        dst->text = NULL;
        return 0;
    }
    if (!src->text) {
        dst->text = strdup("");
        return dst->text ? 0 : -1;
    }
    dst->text = strdup(src->text);
    return dst->text ? 0 : -1;
}

void week7_free_prepared(SqlValue *vals, size_t n) {
    if (!vals) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        free(vals[i].text);
    }
    free(vals);
}

int week7_prepare_insert_values(const InsertStmt *stmt, SqlValue **out_vals, size_t *out_n, int64_t *out_assigned_id) {
    if (!stmt || !stmt->table || !out_vals || !out_n || !out_assigned_id) {
        return -1;
    }
    *out_vals = NULL;
    *out_n = 0;
    if (week7_ensure_loaded(stmt->table) != 0) {
        return -1;
    }
    Ent *e = find_ent(stmt->table);
    if (!e || !e->id_pk) {
        return 1;
    }

    size_t ncol = e->ncol;
    if (stmt->value_count != ncol && stmt->value_count + 1 != ncol) {
        return -1;
    }

    SqlValue *vals = calloc(ncol, sizeof *vals);
    if (!vals) {
        return -1;
    }

    int64_t aid = e->next_id;
    if (stmt->value_count + 1 == ncol) {
        if (int64_to_sqlvalue(aid, &vals[0]) != 0) {
            week7_free_prepared(vals, ncol);
            return -1;
        }
        for (size_t i = 0; i < stmt->value_count; i++) {
            if (dup_sqlvalue(&stmt->values[i], &vals[1 + i]) != 0) {
                week7_free_prepared(vals, ncol);
                return -1;
            }
        }
    } else {
        if (int64_to_sqlvalue(aid, &vals[0]) != 0) {
            week7_free_prepared(vals, ncol);
            return -1;
        }
        for (size_t i = 1; i < ncol; i++) {
            if (dup_sqlvalue(&stmt->values[i], &vals[i]) != 0) {
                week7_free_prepared(vals, ncol);
                return -1;
            }
        }
    }

    *out_vals = vals;
    *out_n = ncol;
    *out_assigned_id = aid;
    return 0;
}

int week7_on_append_success(const char *table, int64_t assigned_id, size_t row_index) {
    Ent *e = find_ent(table);
    if (!e || !e->id_pk || !e->tree) {
        return 0;
    }
    if (bplus_insert_or_replace(e->tree, assigned_id, row_index) != 0) {
        return -1;
    }
    if (assigned_id >= e->next_id) {
        e->next_id = assigned_id + 1;
    }
    return 0;
}

int week7_lookup_row(const char *table, int64_t id, size_t *out_row_index) {
    if (!table || !out_row_index) {
        return -1;
    }
    if (week7_ensure_loaded(table) != 0) {
        return -1;
    }
    Ent *e = find_ent(table);
    if (!e || !e->id_pk || !e->tree) {
        return -1;
    }
    return bplus_search(e->tree, id, out_row_index);
}
