/* WEEK7: B+ tree / primary-key index (see docs/weeks/WEEK7/) */
#include "week7/bplus_tree.h"

#include <stdlib.h>
#include <string.h>

#define BP_MAX_KEYS 3
#define BP_MAX_CHILDREN (BP_MAX_KEYS + 1)

typedef struct BPNode {
    int leaf;
    int nkeys;
    int64_t keys[BP_MAX_KEYS];
    union {
        size_t payloads[BP_MAX_KEYS];
        struct BPNode *kids[BP_MAX_CHILDREN];
    } u;
    struct BPNode *parent;
    struct BPNode *next;
} BPNode;

struct BPlusTree {
    BPNode *root;
};

static BPNode *node_new(int leaf) {
    BPNode *n = calloc(1, sizeof *n);
    if (!n) {
        return NULL;
    }
    n->leaf = leaf;
    return n;
}

static void node_free(BPNode *n) {
    if (!n) {
        return;
    }
    if (!n->leaf) {
        for (int i = 0; i <= n->nkeys; i++) {
            node_free(n->u.kids[i]);
        }
    }
    free(n);
}

void bplus_destroy(BPlusTree *t) {
    if (!t) {
        return;
    }
    node_free(t->root);
    free(t);
}

BPlusTree *bplus_create(void) {
    BPlusTree *t = malloc(sizeof *t);
    if (!t) {
        return NULL;
    }
    t->root = node_new(1);
    if (!t->root) {
        free(t);
        return NULL;
    }
    return t;
}

static BPNode *find_leaf(const BPlusTree *t, int64_t key) {
    BPNode *x = t->root;
    while (!x->leaf) {
        int i = x->nkeys;
        for (int j = 0; j < x->nkeys; j++) {
            if (key < x->keys[j]) {
                i = j;
                break;
            }
            i = j + 1;
        }
        x = x->u.kids[i];
    }
    return x;
}

int bplus_search(const BPlusTree *t, int64_t key, size_t *payload) {
    if (!t || !t->root || !payload) {
        return -1;
    }
    BPNode *leaf = find_leaf(t, key);
    for (int i = 0; i < leaf->nkeys; i++) {
        if (leaf->keys[i] == key) {
            *payload = leaf->u.payloads[i];
            return 0;
        }
    }
    return -1;
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
