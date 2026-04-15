/* WEEK7: B+ tree / primary-key index (see docs/weeks/WEEK7/) */
#include "week7/bplus_tree.h" // bplus_tree.h 헤더 파일을 포함한다.

#include <stdlib.h> // stdlib.h는 표준 라이브러리 헤더 파일로, 메모리 할당과 해제, 수학 함수 등을 정의한다.
#include <string.h> // string.h는 표준 라이브러리 헤더 파일로, 문자열 함수를 정의한다.

#define BP_MAX_KEYS 3 // BP_MAX_KEYS는 3이다. 왜 3인가? 왜냐하면 B+ 트리는 최대 3개의 키를 가질 수 있기 때문이다. 3개의 기본 차수를 가질 수 있기 때문이다.
#define BP_MAX_CHILDREN (BP_MAX_KEYS + 1) // BP_MAX_CHILDREN는 4이다. 왜 4인가? 왜냐하면 B+ 트리는 최대 3개의 키를 가질 수 있기 때문에 4개의 자식 노드를 가질 수 있기 때문이다.

typedef struct BPNode {// BPNode 구조체 정의
    int leaf; // leaf는 리프 노드인지 내부 노드인지를 나타내는 변수이다.
    int nkeys; // nkeys는 노드에 포함된 키의 개수를 나타내는 변수이다. 키는 여러개 있을 수 있기 때문에 개수를 나타내는 변수가 필요하다.
    int64_t keys[BP_MAX_KEYS]; // keys는 노드에 포함된 키들을 나타내는 배열이다. 키는 여러개 있을 수 있기 때문에 배열로 나타내는 것이 편리하다.
    // union은 공용체로, 여러 타입의 변수를 하나의 메모리 공간에 저장할 수 있게 해준다. 리프 노드인 경우 payloads 배열을 사용하고, 내부 노드인 경우 kids 배열을 사용한다.
    union { 
        size_t payloads[BP_MAX_KEYS]; // payloads는 노드에 포함된 페이로드들을 나타내는 배열이다. 페이로드는 여러개 있을 수 있기 때문에 배열로 나타내는 것이 편리하다.
        struct BPNode *kids[BP_MAX_CHILDREN]; // kids는 노드에 포함된 자식 노드들을 나타내는 배열이다. 자식 노드는 여러개 있을 수 있기 때문에 배열로 나타내는 것이 편리하다.
    } u;
    struct BPNode *parent; // parent는 부모 노드를 나타내는 포인터이다.
    struct BPNode *next;/* next: 리프 체인만 사용. 키 순서상 오른쪽 형제 리프(범위 스캔용). 내부 노드에서는 NULL(미사용). */
} BPNode;

struct BPlusTree {// BPlusTree 구조체 정의
    BPNode *root; // root는 루트 노드를 나타내는 포인터이다.
};

static BPNode *node_new(int leaf) {// node_new 함수 정의 이 함수는 새로운 노드를 생성하는 함수이다.
    BPNode *n = calloc(1, sizeof *n); // calloc은 메모리 할당 함수로, 주어진 크기의 메모리를 할당하고 0으로 초기화한다.
    if (!n) { // 메모리 할당 실패 시 NULL 반환 왜 NULL 반환인가? 왜냐하면 메모리 할당 실패 시 프로그램이 종료되기 때문이다.
        return NULL;
    }
    n->leaf = leaf; // 노드의 leaf 필드를 설정 리프 노드인지 내부 노드인지를 나타내는 변수이다. 1이면 리프 노드, 0이면 내부 노드이다.
    return n; // 생성된 노드 반환 생성된 노드를 반환하는 이유는 생성된 노드를 사용하기 위해서이다. 여기서 n의 값은 BPNode 구조체 포인터이다.
}

static void node_free(BPNode *n) {// node_free 함수 정의 이 함수는 노드를 해제하는 함수이다.
    if (!n) { // 노드가 NULL인 경우 즉, 노드가 생성되지 않은 경우 함수를 종료한다.
        return;
    }
    if (!n->leaf) { // 노드가 내부 노드인 경우 즉, 노드가 리프 노드가 아닌 경우 자식 노드들을 해제한다.
        for (int i = 0; i <= n->nkeys; i++) { // 자식 노드들을 해제한다. 순회방법은 0부터 nkeys까지 순회한다.
            node_free(n->u.kids[i]); // 자식 노드들을 해제한다.
        }
    }
    free(n); // 노드를 해제한다. 노드를 해제하는 이유는 메모리 누수를 방지하기 위해서이다. 여기서 free는 stdlib.h 헤더 파일에 정의된 메모리 해제 함수이다.
}

void bplus_destroy(BPlusTree *t) { // bplus_destroy 함수 정의 이 함수는 BPlusTree를 소멸하는 함수이다.
    if (!t) { // BPlusTree가 NULL인 경우 즉, BPlusTree가 생성되지 않은 경우 함수를 종료한다.
        return;
    }
    node_free(t->root); // 루트 노드를 해제한다. 자식노드들도 함께 해제된다.
    free(t); // BPlusTree를 해제한다. 메모리 누수를 방지하기 위해서이다. 여기서 free는 stdlib.h 헤더 파일에 정의된 메모리 해제 함수이다.
}

BPlusTree *bplus_create(void) { // bplus_create 함수 정의 이 함수는 BPlusTree를 생성하는 함수이다.
    BPlusTree *t = malloc(sizeof *t); // malloc은 메모리 할당 함수로, 주어진 크기의 메모리를 할당한다. 여기서 sizeof *t는 BPlusTree 구조체 크기를 나타낸다.
    if (!t) { // 메모리 할당 실패 시 NULL 반환 왜 NULL 반환인가? 왜냐하면 메모리 할당 실패 시 프로그램이 종료되기 때문이다.
        return NULL;
    }
    t->root = node_new(1); // 루트 노드를 생성한다. 리프 노드로 초기화한다. 
    if (!t->root) { // 루트 노드 생성 실패 시 NULL 반환 왜 NULL 반환인가? 왜냐하면 루트 노드 생성 실패 시 프로그램이 종료되기 때문이다.
        free(t); // BPlusTree를 해제한다. 메모리 누수를 방지하기 위해서이다. 여기서 free는 stdlib.h 헤더 파일에 정의된 메모리 해제 함수이다.
        return NULL;
    }
    return t; // 생성된 BPlusTree 반환 생성된 BPlusTree를 반환하는 이유는 생성된 BPlusTree를 사용하기 위해서이다. 여기서 t의 값은 BPlusTree 구조체 포인터이다.
}

static BPNode *find_leaf(const BPlusTree *t, int64_t key) { // find_leaf 함수 정의 이 함수는 리프 노드를 찾는 함수이다.
    BPNode *x = t->root; // 루트 노드를 찾는다. 루트 노드는 BPlusTree의 최상위 노드이다.
    // 루트 노드가 리프 노드가 아닌 경우 즉, 루트 노드가 내부 노드인 경우 자식 노드를 찾는다.
    while (!x->leaf) {  
        int i = x->nkeys; // 노드에 포함된 키의 개수를 나타내는 변수이다.
        // 노드에 포함된 키들을 순회한다.
        for (int j = 0; j < x->nkeys; j++) { // 순회방법은 0부터 nkeys까지 순회한다.
            // 키가 노드에 포함된 키보다 작은경우
            if (key < x->keys[j]) {
                i = j; 
                break;
            }
            i = j + 1;
        }
        x = x->u.kids[i]; // 한층 아래로 이동한다.
    }
    // 리프 노드를 반환한다. 리프 노드는 노드에 포함된 키들을 정렬한 후 키가 작은 순서대로 정렬된 노드이다.
    return x;
}

int bplus_search(const BPlusTree *t, int64_t key, size_t *payload) { // bplus_search 함수 정의 이 함수는 BPlusTree에서 키를 검색하는 함수이다.
    if (!t || !t->root || !payload) { // BPlusTree가 NULL인 경우 즉, BPlusTree가 생성되지 않은 경우 또는 payload가 NULL인 경우 즉, payload가 생성되지 않은 경우 함수를 종료한다.
        return -1;
    }
    BPNode *leaf = find_leaf(t, key); // 리프 노드를 찾는다.
    for (int i = 0; i < leaf->nkeys; i++) { // 리프 노드에 포함된 키들을 순회한다.
        if (leaf->keys[i] == key) { // 키가 리프 노드에 포함된 키와 같은 경우
            *payload = leaf->u.payloads[i]; // payload를 설정한다.
            return 0; // 0을 반환한다.
        }
    } // 키가 리프 노드에 포함된 키와 같지 않은 경우 -1을 반환한다.
    return -1; // -1을 반환한다.
}

static void leaf_insert_sorted(BPNode *leaf, int64_t key, size_t payload) {
    int i = leaf->nkeys - 1;
    while (i >= 0 && leaf->keys[i] > key) {
        leaf->keys[i + 1] = leaf->keys[i];
        leaf->u.payloads[i + 1] = leaf->u.payloads[i];
        i--;
    }
    leaf->keys[i + 1] = key;
    leaf->u.payloads[i + 1] = payload;
    leaf->nkeys++;
}

/* Split leaf that already holds BP_MAX_KEYS keys; insert (key,payload) into merged order */
static int leaf_split_insert(BPNode *leaf, int64_t key, size_t payload, BPNode **out_right, int64_t *out_sep) {
    int64_t ks[BP_MAX_KEYS + 1];
    size_t ps[BP_MAX_KEYS + 1];
    int n = BP_MAX_KEYS;
    for (int i = 0; i < n; i++) {
        ks[i] = leaf->keys[i];
        ps[i] = leaf->u.payloads[i];
    }
    int pos = n;
    for (int i = 0; i < n; i++) {
        if (key == ks[i]) {
            return -1;
        }
        if (key < ks[i]) {
            pos = i;
            break;
        }
        pos = i + 1;
    }
    for (int i = n; i > pos; i--) {
        ks[i] = ks[i - 1];
        ps[i] = ps[i - 1];
    }
    ks[pos] = key;
    ps[pos] = payload;
    n++;

    int split = (n + 1) / 2;
    BPNode *right = node_new(1);
    if (!right) {
        return -2;
    }
    leaf->nkeys = split;
    for (int i = 0; i < split; i++) {
        leaf->keys[i] = ks[i];
        leaf->u.payloads[i] = ps[i];
    }
    right->nkeys = n - split;
    for (int i = 0; i < right->nkeys; i++) {
        right->keys[i] = ks[split + i];
        right->u.payloads[i] = ps[split + i];
    }
    *out_sep = right->keys[0];
    right->next = leaf->next;
    leaf->next = right;
    right->parent = leaf->parent;
    *out_right = right;
    return 0;
}

static int insert_into_parent(BPlusTree *t, BPNode *left, int64_t sep, BPNode *right);

/* Split internal node `node` that already has BP_MAX_KEYS keys; insert sep at key index `ins_k`,
 * right child at child index `ins_k+1` in merged order */
static int internal_split_insert(BPlusTree *t, BPNode *node, int ins_k, int64_t sep, BPNode *right,
                                 int64_t *out_up, BPNode **out_new_right) {
    int64_t ks[BP_MAX_KEYS + 1];
    BPNode *ch[BP_MAX_CHILDREN + 2];
    int nk = node->nkeys;
    for (int i = 0; i < nk; i++) {
        ks[i] = node->keys[i];
    }
    for (int i = 0; i <= nk; i++) {
        ch[i] = node->u.kids[i];
    }
    for (int i = nk; i > ins_k; i--) {
        ks[i] = ks[i - 1];
    }
    ks[ins_k] = sep;
    for (int i = nk + 1; i > ins_k + 1; i--) {
        ch[i] = ch[i - 1];
    }
    ch[ins_k + 1] = right;
    right->parent = NULL;

    int total = nk + 1;
    int mid = total / 2;
    *out_up = ks[mid];

    BPNode *nr = node_new(0);
    if (!nr) {
        return -2;
    }
    node->nkeys = mid;
    for (int i = 0; i < mid; i++) {
        node->keys[i] = ks[i];
        node->u.kids[i] = ch[i];
        node->u.kids[i]->parent = node;
    }
    node->u.kids[mid] = ch[mid];
    node->u.kids[mid]->parent = node;

    nr->nkeys = total - mid - 1;
    for (int i = 0; i < nr->nkeys; i++) {
        nr->keys[i] = ks[mid + 1 + i];
        nr->u.kids[i] = ch[mid + 1 + i];
        nr->u.kids[i]->parent = nr;
    }
    nr->u.kids[nr->nkeys] = ch[total];
    nr->u.kids[nr->nkeys]->parent = nr;

    *out_new_right = nr;
    return 0;
}

static int insert_into_parent(BPlusTree *t, BPNode *left, int64_t sep, BPNode *right) {
    BPNode *p = left->parent;
    right->parent = p;
    if (!p) {
        BPNode *nr = node_new(0);
        if (!nr) {
            return -2;
        }
        nr->nkeys = 0;
        nr->u.kids[0] = left;
        left->parent = nr;
        nr->keys[0] = sep;
        nr->u.kids[1] = right;
        right->parent = nr;
        nr->nkeys = 1;
        t->root = nr;
        return 0;
    }

    int idx = 0;
    while (idx <= p->nkeys && p->u.kids[idx] != left) {
        idx++;
    }
    if (idx > p->nkeys) {
        return -2;
    }

    if (p->nkeys < BP_MAX_KEYS) {
        for (int j = p->nkeys; j > idx; j--) {
            p->keys[j] = p->keys[j - 1];
            p->u.kids[j + 1] = p->u.kids[j];
        }
        p->keys[idx] = sep;
        p->u.kids[idx + 1] = right;
        right->parent = p;
        p->nkeys++;
        return 0;
    }

    int64_t up = 0;
    BPNode *newp = NULL;
    if (internal_split_insert(t, p, idx, sep, right, &up, &newp) != 0) {
        return -2;
    }
    return insert_into_parent(t, p, up, newp);
}

int bplus_insert_or_replace(BPlusTree *t, int64_t key, size_t payload) {
    if (!t || !t->root) {
        return -2;
    }
    BPNode *leaf = find_leaf(t, key);
    for (int i = 0; i < leaf->nkeys; i++) {
        if (leaf->keys[i] == key) {
            leaf->u.payloads[i] = payload;
            return 0;
        }
    }
    return bplus_insert(t, key, payload);
}

int bplus_insert(BPlusTree *t, int64_t key, size_t payload) {
    if (!t || !t->root) {
        return -2;
    }
    size_t tmp;
    if (bplus_search(t, key, &tmp) == 0) {
        return -1;
    }

    BPNode *leaf = find_leaf(t, key);
    for (int i = 0; i < leaf->nkeys; i++) {
        if (leaf->keys[i] == key) {
            return -1;
        }
    }

    if (leaf->nkeys < BP_MAX_KEYS) {
        leaf_insert_sorted(leaf, key, payload);
        return 0;
    }

    BPNode *right = NULL;
    int64_t sep = 0;
    int r = leaf_split_insert(leaf, key, payload, &right, &sep);
    if (r != 0) {
        return r;
    }
    return insert_into_parent(t, leaf, sep, right);
}
