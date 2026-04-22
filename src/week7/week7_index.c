/* WEEK7: primary-key index + auto id (see docs/weeks/WEEK7/) */
#include "week7/week7_index.h"

#include "csv_storage.h"
#include "week7/bplus_tree.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
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

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n + 1);
    return p;
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
static pthread_rwlock_t g_index_lock = PTHREAD_RWLOCK_INITIALIZER;

static void clear_loaded_state(Ent *e) {
    if (!e) {
        return;
    }
    bplus_destroy(e->tree);
    e->tree = NULL;
    e->next_id = 1;
    e->ncol = 0;
    e->loaded = 0;
    e->id_pk = 0;
}

static int parse_strict_id_value(const char *text, int64_t *out) {
    char *end = NULL;
    long long parsed = 0;

    if (!text || !out || text[0] == '\0') {
        return -1;
    }
    if (isspace((unsigned char)text[0])) {
        return -1;
    }

    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || !end || *end != '\0') {
        return -1;
    }

    *out = (int64_t)parsed;
    return 0;
}

void week7_reset(void) {
    pthread_rwlock_wrlock(&g_index_lock);
    for (size_t i = 0; i < g_n; i++) {
        free(g_ents[i].table);
        bplus_destroy(g_ents[i].tree);
    }
    free(g_ents);
    g_ents = NULL;
    g_n = 0;
    g_cap = 0;
    pthread_rwlock_unlock(&g_index_lock);
}

static Ent *find_ent_unlocked(const char *table) {
    for (size_t i = 0; i < g_n; i++) {
        if (strcmp(g_ents[i].table, table) == 0) {
            return &g_ents[i];
        }
    }
    return NULL;
}

static Ent *alloc_ent_unlocked(const char *table) {
    Ent *e = find_ent_unlocked(table);
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
    g_ents[g_n].table = dup_cstr(table);
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
    CsvTable *t = NULL;
    Ent *e = NULL;
    int64_t mx = 0;
    int rc = -1;

    if (!table) {
        return -1;
    }

    pthread_rwlock_wrlock(&g_index_lock);

    e = alloc_ent_unlocked(table);
    if (!e) {
        goto cleanup;
    }
    if (e->loaded) {
        rc = 0;
        goto cleanup;
    }

    clear_loaded_state(e);

    if (csv_storage_read_table(table, &t) != 0 || !t) {
        goto cleanup;
    }
    if (t->header_count == 0) {
        goto cleanup;
    }

    e->ncol = t->header_count;
    e->id_pk = (ascii_strcasecmp(t->headers[0], "id") == 0);
    if (!e->id_pk) {
        e->loaded = 1;
        rc = 0;
        goto cleanup;
    }

    e->tree = bplus_create();
    if (!e->tree) {
        goto cleanup;
    }

    for (size_t r = 0; r < t->row_count; r++) {
        int64_t id = 0;
        if (parse_strict_id_value(t->rows[r][0], &id) != 0) {
            goto cleanup;
        }
        if (id > mx) {
            mx = id;
        }
        if (bplus_insert(e->tree, id, r) != 0) {
            goto cleanup;
        }
    }

    e->next_id = mx + 1;
    e->loaded = 1;
    rc = 0;

cleanup:
    if (rc != 0 && e) {
        clear_loaded_state(e);
    }
    csv_storage_free_table(t);
    pthread_rwlock_unlock(&g_index_lock);
    return rc;
}

int week7_table_has_id_pk(const char *table) {
    Ent *e = NULL;
    int rc = 0;

    pthread_rwlock_rdlock(&g_index_lock);
    e = find_ent_unlocked(table);
    rc = (e && e->loaded && e->id_pk) ? 1 : 0;
    pthread_rwlock_unlock(&g_index_lock);
    return rc;
}

static int int64_to_sqlvalue(int64_t v, SqlValue *dst) {
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", (long long)v);
    dst->kind = SQL_VALUE_INT;
    dst->text = dup_cstr(buf);
    return dst->text ? 0 : -1;
}

static int dup_sqlvalue(const SqlValue *src, SqlValue *dst) {
    dst->kind = src->kind;
    if (src->kind == SQL_VALUE_NULL) {
        dst->text = NULL;
        return 0;
    }
    if (!src->text) {
        dst->text = dup_cstr("");
        return dst->text ? 0 : -1;
    }
    dst->text = dup_cstr(src->text);
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

int week7_prepare_insert_values(const InsertStmt *stmt, SqlValue **out_vals, size_t *out_n,
                                int64_t *out_assigned_id) {
    Ent *e = NULL;
    size_t ncol = 0;
    SqlValue *vals = NULL;
    int64_t aid = 0;

    if (!stmt || !stmt->table || !out_vals || !out_n || !out_assigned_id) {
        return -1;
    }

    *out_vals = NULL;
    *out_n = 0;

    if (week7_ensure_loaded(stmt->table) != 0) {
        return -1;
    }

    pthread_rwlock_rdlock(&g_index_lock);
    e = find_ent_unlocked(stmt->table);
    if (!e || !e->id_pk) {
        pthread_rwlock_unlock(&g_index_lock);
        return 1;
    }

    ncol = e->ncol;
    aid = e->next_id;
    pthread_rwlock_unlock(&g_index_lock);

    if (stmt->value_count != ncol && stmt->value_count + 1 != ncol) {
        return -1;
    }

    vals = calloc(ncol, sizeof *vals);
    if (!vals) {
        return -1;
    }

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
    Ent *e = NULL;
    int rc = 0;

    pthread_rwlock_wrlock(&g_index_lock);
    e = find_ent_unlocked(table);
    if (!e || !e->id_pk || !e->tree) {
        pthread_rwlock_unlock(&g_index_lock);
        return 0;
    }
    if (bplus_insert(e->tree, assigned_id, row_index) != 0) {
        rc = -1;
        goto cleanup;
    }
    if (assigned_id >= e->next_id) {
        e->next_id = assigned_id + 1;
    }

cleanup:
    pthread_rwlock_unlock(&g_index_lock);
    return rc;
}

int week7_lookup_row(const char *table, int64_t id, size_t *out_row_index) {
    Ent *e = NULL;
    int rc = -1;

    if (!table || !out_row_index) {
        return -1;
    }
    if (week7_ensure_loaded(table) != 0) {
        return -1;
    }

    pthread_rwlock_rdlock(&g_index_lock);
    e = find_ent_unlocked(table);
    if (!e || !e->id_pk || !e->tree) {
        goto cleanup;
    }
    rc = bplus_search(e->tree, id, out_row_index);

cleanup:
    pthread_rwlock_unlock(&g_index_lock);
    return rc;
}
