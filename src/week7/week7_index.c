/* WEEK7: primary-key index + auto id (see docs/weeks/WEEK7/) */
#include "week7/week7_index.h"

#include "csv_storage.h"
#include "week7/bplus_tree.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ASCII 기준 대소문자 무시 비교(헤더 'id' 판별용) */
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

/* 테이블별 WEEK7 상태 캐시 */
typedef struct Ent {
    char *table;      /* 테이블 이름 */
    BPlusTree *tree;  /* id -> row_index 인덱스(B+ 트리), id PK 아니면 NULL */
    int64_t next_id;  /* 자동 증가로 부여할 다음 id */
    size_t ncol;      /* CSV 헤더 컬럼 수 */
    int loaded;       /* 1이면 CSV 스캔/판별이 끝난 상태 */
    int id_pk;        /* 1이면 첫 헤더가 id */
} Ent;

static Ent *g_ents;   /* 테이블 상태 동적 배열 */
static size_t g_n;    /* 사용 중 엔트리 수 */
static size_t g_cap;  /* 할당 용량 */

/* 테스트/재실행용: 전역 캐시 초기화 */
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

/* 테이블 이름으로 엔트리 조회(없으면 NULL) */
static Ent *find_ent(const char *table) {
    for (size_t i = 0; i < g_n; i++) {
        if (strcmp(g_ents[i].table, table) == 0) {
            return &g_ents[i];
        }
    }
    return NULL;
}

/* get-or-create: 기존 엔트리 재사용, 없으면 새 엔트리 생성 */
static Ent *alloc_ent(const char *table) {
    Ent *e = find_ent(table);
    if (e) {
        return e;
    }
    if (g_n >= g_cap) { /* 용량 부족 시 2배 확장(최초 4) */
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

/* 테이블 상태를 보장: CSV를 한 번만 읽어 id PK 여부/인덱스/next_id를 초기화 */
int week7_ensure_loaded(const char *table) {
    if (!table) {
        return -1;
    }
    Ent *e = alloc_ent(table);
    if (!e) {
        return -1;
    }
    if (e->loaded) { /* 멱등: 이미 로드됐으면 재사용 */
        return 0;
    }

    CsvTable *t = NULL;
    if (csv_storage_read_table(table, &t) != 0 || !t) {
        return -1;
    }
    if (t->header_count == 0) { /* 헤더 없는 CSV는 비정상 */
        csv_storage_free_table(t);
        return -1;
    }

    e->ncol = t->header_count;                        /* INSERT 검증용 컬럼 수 */
    e->id_pk = (ascii_strcasecmp(t->headers[0], "id") == 0); /* 첫 컬럼 id인지 */
    if (!e->id_pk) {                                  /* id PK 테이블이 아니면 */
        e->tree = NULL;                               /* 인덱스 비활성 */
        e->loaded = 1;                                /* 판별 완료 */
        csv_storage_free_table(t);
        return 0;
    }

    e->tree = bplus_create(); /* id PK면 B+ 트리 생성 */
    if (!e->tree) {
        csv_storage_free_table(t);
        return -1;
    }

    int64_t mx = 0; /* CSV 내 최대 id 추적(자동 증가 시작점 계산) */
    for (size_t r = 0; r < t->row_count; r++) {
        int64_t id = strtoll(t->rows[r][0], NULL, 10); /* row의 첫 컬럼을 id로 해석 */
        if (id > mx) {
            mx = id;
        }
        /* 중복 id가 있으면 마지막 행(row_index=r)으로 덮어쓴다. */
        if (bplus_insert_or_replace(e->tree, id, r) != 0) {
            csv_storage_free_table(t);
            return -1;
        }
    }
    e->next_id = mx + 1; /* 다음 자동 id */
    e->loaded = 1;       /* 로드 완료 */
    csv_storage_free_table(t);
    return 0;
}

/* 인덱스 사용 가능 여부 조회 */
int week7_table_has_id_pk(const char *table) {
    Ent *e = find_ent(table);
    return (e && e->loaded && e->id_pk) ? 1 : 0;
}

/* int64 -> SQL_VALUE_INT 문자열 변환 */
static int int64_to_sqlvalue(int64_t v, SqlValue *dst) {
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", (long long)v);
    dst->kind = SQL_VALUE_INT;
    dst->text = strdup(buf);
    return dst->text ? 0 : -1;
}

// dup_sqlvalue 함수 정의 이 함수는 SqlValue를 깊은 복사하는 함수이다.
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

/* week7_prepare_insert_values가 만든 임시 배열 해제 */
void week7_free_prepared(SqlValue *vals, size_t n) {
    if (!vals) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        free(vals[i].text);
    }
    free(vals);
}

/*
 * id PK 테이블용 INSERT 값 준비.
 * - value_count == ncol-1: id 생략 INSERT -> 맨 앞에 자동 id를 채운다.
 * - value_count == ncol:   id 자리에 값이 있어도 정책상 자동 id로 덮어쓴다.
 * 반환:
 *   0  : out_vals/out_n 사용(호출측 free 필요)
 *   1  : id PK 아님(원본 stmt 사용)
 *  -1  : 오류
 */
int week7_prepare_insert_values(const InsertStmt *stmt, SqlValue **out_vals, size_t *out_n, int64_t *out_assigned_id) {
    if (!stmt || !stmt->table || !out_vals || !out_n || !out_assigned_id) {
        return -1;
    }
    *out_vals = NULL;
    *out_n = 0;
    if (week7_ensure_loaded(stmt->table) != 0) { /* 상태 준비 */
        return -1;
    }
    Ent *e = find_ent(stmt->table);
    if (!e || !e->id_pk) { /* id PK 아니면 가공하지 않음 */
        return 1;
    }

    size_t ncol = e->ncol;
    /* 허용 입력: id 제외(ncol-1) 또는 id 포함(ncol). 그 외는 컬럼 불일치 */
    if (stmt->value_count != ncol && stmt->value_count + 1 != ncol) {
        return -1;
    }

    SqlValue *vals = calloc(ncol, sizeof *vals);
    if (!vals) {
        return -1;
    }

    int64_t aid = e->next_id; /* 이번 INSERT에 부여할 id */
    if (stmt->value_count + 1 == ncol) {
        /* id를 생략한 INSERT: values를 한 칸 뒤로 밀어 복사 */
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
        /* id를 포함해도 정책상 자동 id로 vals[0]을 덮는다. */
        if (int64_to_sqlvalue(aid, &vals[0]) != 0) {
            week7_free_prepared(vals, ncol);
            return -1;
        }
        for (size_t i = 1; i < ncol; i++) {
            /* 원본의 1..ncol-1 컬럼을 그대로 복사(0번 컬럼은 새 id) */
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

/* append 성공 직후: (assigned_id -> row_index)를 인덱스에 반영 */
int week7_on_append_success(const char *table, int64_t assigned_id, size_t row_index) {
    Ent *e = find_ent(table);
    if (!e || !e->id_pk || !e->tree) { /* 인덱스 비대상 테이블은 no-op */
        return 0;
    }
    if (bplus_insert_or_replace(e->tree, assigned_id, row_index) != 0) {
        return -1;
    }
    /* 외부에서 더 큰 id가 들어온 케이스까지 포함해 next_id 보정 */
    if (assigned_id >= e->next_id) {
        e->next_id = assigned_id + 1;
    }
    return 0;
}

/* SELECT WHERE id=... 경로: row_index 조회 */
int week7_lookup_row(const char *table, int64_t id, size_t *out_row_index) {
    if (!table || !out_row_index) {
        return -1;
    }
    if (week7_ensure_loaded(table) != 0) { /* 지연 로드 보장 */
        return -1;
    }
    Ent *e = find_ent(table);
    if (!e || !e->id_pk || !e->tree) { /* 인덱스 미사용 테이블 */
        return -1;
    }
    return bplus_search(e->tree, id, out_row_index);
}