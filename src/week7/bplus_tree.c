/* WEEK7: B+ tree / primary-key index (see docs/weeks/WEEK7/) */
#include "week7/bplus_tree.h" /* 공개 API 선언 */

#include <stdlib.h> /* calloc, malloc, free */

#define BP_MAX_KEYS 3                        /* 노드당 키 상한(과제 차수) */
#define BP_MAX_CHILDREN (BP_MAX_KEYS + 1)  /* 내부 노드 자식 상한 = 키 개수 + 1 */

typedef struct BPNode {
    int leaf;                 /* 1: 리프(키+payload), 0: 내부(키+kids) */
    int nkeys;                /* 현재 노드에 들어 있는 키 개수 */
    int64_t keys[BP_MAX_KEYS];/* 키 슬롯(정렬 유지) */
    union {                   /* 리프/내부에 따라 같은 메모리를 다르게 해석 */
        size_t payloads[BP_MAX_KEYS];   /* 리프: 키와 1:1 대응하는 행 인덱스 등 */
        struct BPNode *kids[BP_MAX_CHILDREN]; /* 내부: 자식 포인터, 개수는 nkeys+1 */
    } u;
    struct BPNode *parent;    /* 부모(분할 후 insert_into_parent에서 사용) */
    struct BPNode *next;      /* 리프만: 키 순 오른쪽 형제 리프. 내부는 NULL */
} BPNode;

struct BPlusTree {
    BPNode *root; /* 트리 루트(빈 트리도 리프 한 노드로 시작) */
};

static BPNode *node_new(int leaf) { /* leaf: 1이면 리프, 0이면 내부 노드 할당 */
    BPNode *n = calloc(1, sizeof *n); /* 구조체 0 초기화(next/parent NULL 등) */
    if (!n) {                         /* 할당 실패 */
        return NULL;                  /* 호출측에 실패 전달 */
    }
    n->leaf = leaf; /* 노드 종류 기록 */
    return n;       /* 새 노드 포인터 반환 */
}

static void node_free(BPNode *n) { /* 재귀적으로 서브트리 해제 */
    if (!n) {      /* NULL 가드 */
        return;    /* 할 일 없음 */
    }
    if (!n->leaf) { /* 내부 노드면 자식부터 해제 */
        for (int i = 0; i <= n->nkeys; i++) { /* 자식은 항상 nkeys+1개 */
            node_free(n->u.kids[i]);        /* 각 자식 서브트리 해제 */
        }
    }
    free(n); /* 현재 노드 블록 반환 */
}

void bplus_destroy(BPlusTree *t) { /* 트리 전체 파괴 */
    if (!t) {           /* NULL 가드 */
        return;         /* 할 일 없음 */
    }
    node_free(t->root); /* 루트부터 하위 전부 free */
    free(t);            /* BPlusTree 래퍼 해제 */
}

BPlusTree *bplus_create(void) { /* 빈 트리: 루트만 있는 리프 */
    BPlusTree *t = malloc(sizeof *t); /* 트리 핸들 할당 */
    if (!t) {                           /* 핸들 할당 실패 */
        return NULL;                    /* 실패 */
    }
    t->root = node_new(1); /* 빈 데이터를 담을 단일 리프를 루트로 둠 */
    if (!t->root) {        /* 루트 생성 실패 */
        free(t);           /* 핸들만이라도 정리 */
        return NULL;       /* 실패 */
    }
    return t; /* 성공: nkeys==0 인 리프 루트 */
}

static BPNode *find_leaf(const BPlusTree *t, int64_t key) { /* key가 속할 리프까지 내려감 */
    BPNode *x = t->root; /* 탐색 시작은 루트 */
    while (!x->leaf) {   /* 리프가 아니면 한 레벨씩 하강 */
        int i = x->nkeys; /* 기본: 맨 오른쪽 자식 인덱스 후보 */
        for (int j = 0; j < x->nkeys; j++) { /* 내부 키로 구간 결정 */
            if (key < x->keys[j]) { /* key가 j번 구분키보다 작으면 */
                i = j;                /* j번 자식(왼쪽 구간)으로 */
                break;                /* 구간 확정 */
            }
            i = j + 1; /* key가 keys[j] 이상이면 다음 구간으로 밀기 */
        }
        x = x->u.kids[i]; /* 선택한 자식으로 이동(한 레벨 하강) */
    }
    return x; /* 리프 도착 */
}

int bplus_search(const BPlusTree *t, int64_t key, size_t *payload) { /* 키 검색 */
    if (!t || !t->root || !payload) { /* 입력 무효 */
        return -1;                    /* 실패(미발견과 동일 코드) */
    }
    BPNode *leaf = find_leaf(t, key); /* 이 키가 들어갈 리프(있었다면 여기) */
    for (int i = 0; i < leaf->nkeys; i++) { /* 리프 안에서 선형 탐색 */
        if (leaf->keys[i] == key) {        /* 키 일치 */
            *payload = leaf->u.payloads[i]; /* 연결된 payload 기록 */
            return 0;                         /* 성공 */
        }
    }
    return -1; /* 리프에 없음: 미발견 */
}

static void leaf_insert_sorted(BPNode *leaf, int64_t key, size_t payload) { /* 리프에 여유 있을 때 삽입 */
    int i = leaf->nkeys - 1; /* 맨 끝 키부터 앞으로 스캔 */
    while (i >= 0 && leaf->keys[i] > key) { /* 삽입 위치까지 키가 더 큰 동안 */
        leaf->keys[i + 1] = leaf->keys[i];           /* 키 한 칸 오른쪽으로 이동 */
        leaf->u.payloads[i + 1] = leaf->u.payloads[i]; /* payload 동반 이동 */
        i--;                                       /* 왼쪽으로 */
    }
    leaf->keys[i + 1] = key;               /* 빈 슬롯에 키 기록 */
    leaf->u.payloads[i + 1] = payload;   /* 동일 인덱스에 payload */
    leaf->nkeys++;                       /* 키 개수 증가 */
}

/* 이미 BP_MAX_KEYS개 꽉 찬 리프에 (key,payload)를 넣기 위해 임시로 BP_MAX_KEYS+1칸을 만든 뒤 둘로 쪼갬 */
static int leaf_split_insert(BPNode *leaf, int64_t key, size_t payload, BPNode **out_right, int64_t *out_sep) {
    int64_t ks[BP_MAX_KEYS + 1]; /* 병합 정렬용 임시 키 버퍼(최대 4개) */
    size_t ps[BP_MAX_KEYS + 1];  /* 병합 정렬용 임시 payload 버퍼 */
    int n = BP_MAX_KEYS;         /* 현재 leaf에 있던 키 개수(= BP_MAX_KEYS) */
    for (int i = 0; i < n; i++) { /* leaf → 임시 배열로 복사 */
        ks[i] = leaf->keys[i];              /* 키 복사 */
        ps[i] = leaf->u.payloads[i];        /* payload 복사 */
    }
    int pos = n; /* 새 키 삽입 인덱스 기본값: 맨 끝(n) */
    for (int i = 0; i < n; i++) { /* 정렬 순서로 삽입 위치 탐색 */
        if (key == ks[i]) { /* 중복 키면 */
            return -1;      /* 삽입 거부 */
        }
        if (key < ks[i]) { /* 처음으로 만나는 더 큰 키 앞이 삽입 위치 */
            pos = i;       /* 삽입 인덱스 확정 */
            break;         /* 루프 종료 */
        }
        pos = i + 1; /* 아직 더 크면 다음 칸 후보로 */
    }
    for (int i = n; i > pos; i--) { /* pos부터 오른쪽을 한 칸씩 밀기 */
        ks[i] = ks[i - 1];         /* 키 이동 */
        ps[i] = ps[i - 1];         /* payload 이동 */
    }
    ks[pos] = key;       /* 확정된 위치에 새 키 */
    ps[pos] = payload;   /* 짝 payload */
    n++;                 /* 총 키 개수 = BP_MAX_KEYS + 1 */

    int split = (n + 1) / 2;     /* 왼쪽 리프에 줄 키 개수(상한 반분할) */
    BPNode *right = node_new(1); /* 오른쪽 절반을 담을 새 리프 */
    if (!right) {        /* 할당 실패 */
        return -2;       /* 메모리 오류 */
    }
    leaf->nkeys = split; /* 기존 leaf는 앞쪽 split개만 유지 */
    for (int i = 0; i < split; i++) { /* 왼쪽 리프에 복사 */
        leaf->keys[i] = ks[i];              /* 키 */
        leaf->u.payloads[i] = ps[i];        /* payload */
    }
    right->nkeys = n - split; /* 오른쪽 리프 키 개수 */
    for (int i = 0; i < right->nkeys; i++) { /* 오른쪽 리프에 복사 */
        right->keys[i] = ks[split + i];              /* 키 */
        right->u.payloads[i] = ps[split + i];        /* payload */
    }
    *out_sep = right->keys[0]; /* 부모에 올릴 구분키: 오른쪽 리프의 최소키 */
    right->next = leaf->next;  /* 오른쪽이 기존 오른쪽 이웃을 가리키게 */
    leaf->next = right;        /* 왼쪽 리프의 next를 새 오른쪽 리프로 */
    right->parent = leaf->parent; /* 부모는 동일(이후 insert_into_parent에서 갱신 가능) */
    *out_right = right; /* 호출측에 새 리프 포인터 전달 */
    return 0;           /* 성공 */
}

static int insert_into_parent(BPlusTree *t, BPNode *left, int64_t sep, BPNode *right); /* 전방 선언 */

/* 내부 노드가 BP_MAX_KEYS개 키로 꽉 찬 상태에서 sep·right를 끼워 넣고 분할 */
static int internal_split_insert(BPlusTree *t, BPNode *node, int ins_k, int64_t sep, BPNode *right,
                                 int64_t *out_up, BPNode **out_new_right) {
    int64_t ks[BP_MAX_KEYS + 1];      /* 병합용 키 임시배열 */
    BPNode *ch[BP_MAX_CHILDREN + 2];  /* 병합용 자식 임시배열(삽입으로 +1) */
    int nk = node->nkeys;             /* 분할 전 키 개수 */
    for (int i = 0; i < nk; i++) {    /* 키 복사 */
        ks[i] = node->keys[i];        /* i번째 키 복사 */
    }
    for (int i = 0; i <= nk; i++) {   /* 자식 포인터 복사(nk+1개) */
        ch[i] = node->u.kids[i];      /* i번째 자식 포인터 복사 */
    }
    for (int i = nk; i > ins_k; i--) { /* 키 슬롯 오른쪽으로 밀기 */
        ks[i] = ks[i - 1];             /* 한 칸 오른쪽으로 이동 */
    }
    ks[ins_k] = sep; /* 새 구분키 삽입 */
    for (int i = nk + 1; i > ins_k + 1; i--) { /* 자식 슬롯 오른쪽으로 밀기 */
        ch[i] = ch[i - 1];                     /* 한 칸 오른쪽으로 이동 */
    }
    ch[ins_k + 1] = right; /* 새 자식 포인터 배치 */
    right->parent = NULL;  /* 아래에서 올바른 부모로 다시 연결 */

    int total = nk + 1;     /* 병합 후 키 개수 */
    int mid = total / 2;    /* 부모로 밀어 올릴 키 인덱스(왼쪽·오른쪽 분할 경계) */
    *out_up = ks[mid];      /* 상위로 전파할 키(중간 키) */

    BPNode *nr = node_new(0); /* 오른쪽 내부 노드(또는 형제 내부) */
    if (!nr) {       /* 할당 실패 */
        return -2;   /* 메모리 오류 */
    }
    node->nkeys = mid; /* 왼쪽 노드 키 개수: mid개(구분키 미만 슬롯) */
    for (int i = 0; i < mid; i++) { /* 왼쪽 키·자식 재배치 */
        node->keys[i] = ks[i];               /* 왼쪽 i번째 키 */
        node->u.kids[i] = ch[i];             /* 왼쪽 i번째 자식 */
        node->u.kids[i]->parent = node;      /* 부모 포인터 정정 */
    }
    node->u.kids[mid] = ch[mid];             /* 가운데 경계 자식 */
    node->u.kids[mid]->parent = node;       /* 부모 포인터 정정 */

    nr->nkeys = total - mid - 1; /* 오른쪽 노드 키 개수(중간키 제외) */
    for (int i = 0; i < nr->nkeys; i++) { /* 오른쪽 키·자식 */
        nr->keys[i] = ks[mid + 1 + i];           /* 오른쪽 i번째 키 */
        nr->u.kids[i] = ch[mid + 1 + i];         /* 오른쪽 i번째 자식 */
        nr->u.kids[i]->parent = nr;              /* 부모를 nr로 */
    }
    nr->u.kids[nr->nkeys] = ch[total];         /* 오른쪽 마지막 자식 */
    nr->u.kids[nr->nkeys]->parent = nr;        /* 부모를 nr로 */

    *out_new_right = nr; /* 새 오른쪽 내부 노드 반환 */
    return 0;            /* 분할 성공 */
}

static int insert_into_parent(BPlusTree *t, BPNode *left, int64_t sep, BPNode *right) { /* 분할 후 부모에 연결 */
    BPNode *p = left->parent; /* left의 부모 */
    right->parent = p;        /* right도 같은 부모 아래로(아래 분기에서 조정) */
    if (!p) { /* 부모가 없음: 루트 분할 → 새 루트 생성 */
        BPNode *nr = node_new(0); /* 새 내부 루트 */
        if (!nr) {       /* 할당 실패 */
            return -2;   /* 메모리 오류 */
        }
        nr->nkeys = 0;           /* 잠깐 0으로 두고 자식만 연결 */
        nr->u.kids[0] = left;    /* 왼쪽 자식 */
        left->parent = nr;       /* 부모 갱신 */
        nr->keys[0] = sep;       /* 루트의 첫 구분키 */
        nr->u.kids[1] = right;  /* 오른쪽 자식 */
        right->parent = nr;      /* 부모 갱신 */
        nr->nkeys = 1;           /* 키 1개짜리 내부 루트 */
        t->root = nr;            /* 트리 루트 상승 */
        return 0;                /* 새 루트 연결 완료 */
    }

    int idx = 0; /* p에서 left가 몇 번 자식인지 */
    while (idx <= p->nkeys && p->u.kids[idx] != left) { /* 선형 탐색 */
        idx++; /* 다음 자식 인덱스 */
    }
    if (idx > p->nkeys) { /* left를 못 찾음: 불변식 깨짐 */
        return -2;        /* 오류 */
    }

    if (p->nkeys < BP_MAX_KEYS) { /* 부모에 여유 있으면 키·자식 삽입만 */
        for (int j = p->nkeys; j > idx; j--) { /* 구분키·자식 오른쪽으로 밀기 */
            p->keys[j] = p->keys[j - 1];       /* 구분키 한 칸 이동 */
            p->u.kids[j + 1] = p->u.kids[j];   /* 자식 포인터 한 칸 이동 */
        }
        p->keys[idx] = sep;           /* idx와 idx+1 사이 구분값 */
        p->u.kids[idx + 1] = right;   /* 새 오른쪽 자식 */
        right->parent = p;            /* 부모 연결 */
        p->nkeys++;                   /* 키 개수 증가 */
        return 0;                     /* 부모 삽입 완료 */
    }

    int64_t up = 0;      /* 재귀적으로 올릴 구분키 */
    BPNode *newp = NULL; /* 부모 분할 시 생기는 새 노드 */
    if (internal_split_insert(t, p, idx, sep, right, &up, &newp) != 0) { /* 부모도 분할 */
        return -2; /* 실패 */
    }
    return insert_into_parent(t, p, up, newp); /* 재귀: 한 단계 위에도 연결 */
}

int bplus_insert_or_replace(BPlusTree *t, int64_t key, size_t payload) { /* 있으면 payload만 갱신 */
    if (!t || !t->root) { /* 트리 무효 */
        return -2;      /* 오류 */
    }
    BPNode *leaf = find_leaf(t, key); /* 해당 키 구간 리프 */
    for (int i = 0; i < leaf->nkeys; i++) { /* 리프 스캔 */
        if (leaf->keys[i] == key) {        /* 동일 키 발견 */
            leaf->u.payloads[i] = payload; /* 마지막 행 반영 등 */
            return 0;                        /* 갱신 완료 */
        }
    }
    return bplus_insert(t, key, payload); /* 없으면 일반 삽입 */
}

int bplus_insert(BPlusTree *t, int64_t key, size_t payload) { /* 신규 키 삽입 */
    if (!t || !t->root) { /* 트리 무효 */
        return -2;      /* 오류 */
    }
    size_t tmp;                        /* bplus_search용 더미 */
    if (bplus_search(t, key, &tmp) == 0) { /* 이미 존재 */
        return -1;                         /* 중복 삽입 거부 */
    }

    BPNode *leaf = find_leaf(t, key); /* 삽입 대상 리프 */
    for (int i = 0; i < leaf->nkeys; i++) { /* 방어적 중복 검사 */
        if (leaf->keys[i] == key) {        /* 리프에 이미 있음 */
            return -1;                     /* 중복 */
        }
    }

    if (leaf->nkeys < BP_MAX_KEYS) { /* 리프에 빈 슬롯 */
        leaf_insert_sorted(leaf, key, payload); /* 정렬 유지 삽입 */
        return 0;                                  /* 삽입 완료 */
    }

    BPNode *right = NULL; /* 분할 시 새 오른쪽 리프 */
    int64_t sep = 0;      /* 부모에 줄 구분키 */
    int r = leaf_split_insert(leaf, key, payload, &right, &sep); /* 리프 분할+삽입 */
    if (r != 0) { /* -1 중복, -2 OOM */
        return r; /* 그대로 전달 */
    }
    return insert_into_parent(t, leaf, sep, right); /* 부모 체인 갱신 */
}
