#include "storage_cache.h"

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define NIL (-1)

typedef struct Node {
    int key;
    int value;

    /* LRU list links (most-recent-first doubly linked list). */
    int prev;
    int next;

    /* Hash bucket chain links (separate chaining). */
    int hprev;
    int hnext;

    /* Which bucket this node lives in, so removing it never needs to
     * re-hash the key. */
    size_t bucket;
} Node;

struct StorageCache {
    int capacity;

    Node* nodes;      /* pool of `capacity` nodes, indexed 0..capacity-1 */

    int head;         /* most recently used node index, NIL if empty */
    int tail;         /* least recently used node index, NIL if empty */

    /* Head of the free-node stack (linked via ->next). The cache is full
     * exactly when this is NIL: every node starts on this stack, is
     * popped exactly once when first used, and is never pushed back
     * (eviction reuses a node's slot directly instead), so freeHead
     * reaching NIL and staying there is equivalent to size == capacity. */
    int freeHead;

    int* buckets;      /* hash table: bucket -> head node index, or NIL */
    size_t bucketCount; /* power of two */

    /* Per-instance mixing salt, so the bucket a key lands in can't be
     * predicted from the key alone across different cache instances/runs.
     * This raises the bar against a casual/blind adversary picking keys
     * designed to collide into one bucket and degrading get/put toward
     * O(capacity); it is a best-effort mitigation (time+address+a
     * process-lifetime counter), NOT a cryptographic guarantee -- do not
     * rely on it alone if this cache is ever exposed to a determined
     * attacker who can observe process start time. */
    unsigned int seed;
};

/* Incremented once per createCache call so multiple caches created
 * within the same clock second still get distinct seeds. */
static unsigned int g_createCount = 0;

static size_t hashKey(int key, size_t bucketCount, unsigned int seed) {
    unsigned int k = (unsigned int)key ^ seed;
    k ^= k >> 16;
    k *= 0x85ebca6bU;
    k ^= k >> 13;
    k *= 0xc2b2ae35U;
    k ^= k >> 16;
    return (size_t)k & (bucketCount - 1);
}

/* Smallest power of two >= n. Only ever called with n bounded well
 * below SIZE_MAX by createCache's own overflow guard, so no additional
 * overflow check is needed here. */
static size_t nextPowerOfTwo(size_t n) {
    size_t p = 8;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

StorageCache* createCache(int capacity) {
    if (capacity <= 0) {
        return NULL;
    }

    size_t cap = (size_t)capacity;
    /* Guard against `sizeof(Node) * cap` wrapping size_t (possible on
     * 32-bit builds for a large capacity), which would otherwise silently
     * allocate an undersized node pool and let later writes run past it. */
    if (cap > SIZE_MAX / sizeof(Node)) {
        return NULL;
    }

    StorageCache* cache = (StorageCache*)malloc(sizeof(StorageCache));
    if (cache == NULL) {
        return NULL;
    }

    cache->capacity = capacity;
    cache->head = NIL;
    cache->tail = NIL;

    cache->nodes = (Node*)malloc(sizeof(Node) * cap);
    if (cache->nodes == NULL) {
        free(cache);
        return NULL;
    }

    /* Load factor stays <= 0.5 since the cache never holds more than
     * `capacity` keys, so no rehash/resize is ever needed. `cap` is
     * already bounded above by the node-pool guard, so `cap * 2` here
     * cannot overflow size_t. */
    cache->bucketCount = nextPowerOfTwo(cap * 2);
    cache->buckets = (int*)malloc(sizeof(int) * cache->bucketCount);
    if (cache->buckets == NULL) {
        free(cache->nodes);
        free(cache);
        return NULL;
    }
    for (size_t i = 0; i < cache->bucketCount; i++) {
        cache->buckets[i] = NIL;
    }

    /* Chain every node into the free-node stack up front. */
    for (int i = 0; i < capacity; i++) {
        cache->nodes[i].next = (i + 1 < capacity) ? (i + 1) : NIL;
    }
    cache->freeHead = 0;
    cache->seed = (unsigned int)(uintptr_t)cache ^ (unsigned int)time(NULL) ^
                  (g_createCount++ * 0x9E3779B9U);

    return cache;
}

/* Looks up key and, when found, also reports which bucket it lives in
 * (so a subsequent hashRemove doesn't need to re-hash the key). Pass
 * NULL for outBucket if the caller has no use for it. */
static int findNode(StorageCache* cache, int key, size_t* outBucket) {
    size_t bucket = hashKey(key, cache->bucketCount, cache->seed);
    if (outBucket != NULL) {
        *outBucket = bucket;
    }
    int idx = cache->buckets[bucket];
    while (idx != NIL) {
        if (cache->nodes[idx].key == key) {
            return idx;
        }
        idx = cache->nodes[idx].hnext;
    }
    return NIL;
}

/* Inserts node idx (whose ->key is already set) into a caller-supplied
 * bucket, so a miss lookup's already-computed bucket can be reused
 * instead of hashing the key a second time. */
static void hashInsertAt(StorageCache* cache, int idx, size_t bucket) {
    int old = cache->buckets[bucket];

    cache->nodes[idx].bucket = bucket;
    cache->nodes[idx].hprev = NIL;
    cache->nodes[idx].hnext = old;
    if (old != NIL) {
        cache->nodes[old].hprev = idx;
    }
    cache->buckets[bucket] = idx;
}

static void hashRemove(StorageCache* cache, int idx) {
    Node* n = &cache->nodes[idx];
    if (n->hprev != NIL) {
        cache->nodes[n->hprev].hnext = n->hnext;
    } else {
        cache->buckets[n->bucket] = n->hnext;
    }
    if (n->hnext != NIL) {
        cache->nodes[n->hnext].hprev = n->hprev;
    }
}

static void listRemove(StorageCache* cache, int idx) {
    Node* n = &cache->nodes[idx];
    if (n->prev != NIL) {
        cache->nodes[n->prev].next = n->next;
    } else {
        cache->head = n->next;
    }
    if (n->next != NIL) {
        cache->nodes[n->next].prev = n->prev;
    } else {
        cache->tail = n->prev;
    }
}

static void listPushFront(StorageCache* cache, int idx) {
    Node* n = &cache->nodes[idx];
    n->prev = NIL;
    n->next = cache->head;
    if (cache->head != NIL) {
        cache->nodes[cache->head].prev = idx;
    }
    cache->head = idx;
    if (cache->tail == NIL) {
        cache->tail = idx;
    }
}

static void touch(StorageCache* cache, int idx) {
    if (cache->head == idx) {
        return;
    }
    listRemove(cache, idx);
    listPushFront(cache, idx);
}

int get(StorageCache* cache, int key) {
    if (cache == NULL) {
        return -1;
    }

    int idx = findNode(cache, key, NULL);
    if (idx == NIL) {
        return -1;
    }

    touch(cache, idx);
    return cache->nodes[idx].value;
}

void put(StorageCache* cache, int key, int value) {
    if (cache == NULL) {
        return;
    }

    size_t bucket;
    int idx = findNode(cache, key, &bucket);
    if (idx != NIL) {
        cache->nodes[idx].value = value;
        touch(cache, idx);
        return;
    }

    if (cache->freeHead == NIL) {
        /* Cache is full: evict the least recently used entry and reuse
         * its slot. */
        int lru = cache->tail;
        listRemove(cache, lru);
        hashRemove(cache, lru);
        idx = lru;
    } else {
        idx = cache->freeHead;
        cache->freeHead = cache->nodes[idx].next;
    }

    cache->nodes[idx].key = key;
    cache->nodes[idx].value = value;
    hashInsertAt(cache, idx, bucket);
    listPushFront(cache, idx);
}

void freeCache(StorageCache* cache) {
    if (cache == NULL) {
        return;
    }
    free(cache->buckets);
    free(cache->nodes);
    free(cache);
}

#ifdef STORAGE_CACHE_TEST_SUPPORT
/* Test-only: exposes which bucket `key` would hash into for this exact
 * cache instance (its real, per-instance seed included), so tests can
 * deterministically construct hash collisions without duplicating or
 * guessing at hashKey's internals. Not part of the public LRU API. */
size_t storageCacheDebugBucket(StorageCache* cache, int key) {
    return hashKey(key, cache->bucketCount, cache->seed);
}
#endif
