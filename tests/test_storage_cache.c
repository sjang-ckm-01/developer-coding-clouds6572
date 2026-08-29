#include <assert.h>
#include <stdio.h>

#include "storage_cache.h"
#include "storage_cache_test_support.h"

static void test_basic_get_put(void) {
    StorageCache* c = createCache(2);
    assert(get(c, 1) == -1);

    put(c, 1, 10);
    assert(get(c, 1) == 10);

    put(c, 1, 20); /* overwrite */
    assert(get(c, 1) == 20);

    freeCache(c);
}

static void test_eviction_order(void) {
    StorageCache* c = createCache(2);

    put(c, 1, 100);
    put(c, 2, 200);
    assert(get(c, 1) == 100); /* 1 becomes most recently used */

    put(c, 3, 300); /* evicts 2 (least recently used) */
    assert(get(c, 2) == -1);
    assert(get(c, 1) == 100);
    assert(get(c, 3) == 300);

    freeCache(c);
}

static void test_readme_scenario(void) {
    StorageCache* store = createCache(4);

    put(store, 1, 100);
    put(store, 2, 200);
    put(store, 3, 300);
    put(store, 4, 400);

    assert(get(store, 1) == 100);

    put(store, 5, 500);
    assert(get(store, 2) == -1);

    put(store, 6, 600);
    assert(get(store, 3) == -1);
    assert(get(store, 1) == 100);
    assert(get(store, 5) == 500);

    freeCache(store);
}

static void test_invalid_capacity(void) {
    assert(createCache(0) == NULL);
    assert(createCache(-1) == NULL);
}

static void test_capacity_one(void) {
    StorageCache* c = createCache(1);

    assert(get(c, 1) == -1);

    put(c, 1, 10);
    assert(get(c, 1) == 10);

    put(c, 2, 20); /* only slot: evicts 1 immediately */
    assert(get(c, 1) == -1);
    assert(get(c, 2) == 20);

    freeCache(c);
}

static void test_overwrite_then_evict(void) {
    StorageCache* c = createCache(2);

    put(c, 1, 10);
    put(c, 2, 20);
    put(c, 1, 99); /* overwrite must mark key 1 as most recently used */
    assert(get(c, 1) == 99);

    put(c, 3, 30); /* should evict 2 (now the LRU), not 1 */
    assert(get(c, 2) == -1);
    assert(get(c, 1) == 99);
    assert(get(c, 3) == 30);

    freeCache(c);
}

/* Uses the real per-instance seed (via the test-only debug accessor)
 * to find two keys that actually collide into the same bucket for
 * THIS cache instance, so the test deterministically exercises
 * hashRemove's non-head-of-chain unlink branch instead of guessing
 * with a hand-copied, unseeded reimplementation of hashKey. */
static void find_colliding_pair(StorageCache* c, int* out_a, int* out_b) {
    enum { PROBE_RANGE = 1024 };
    int owner[PROBE_RANGE];
    for (int i = 0; i < PROBE_RANGE; i++) {
        owner[i] = -1;
    }
    for (int key = 1; key < 100000; key++) {
        size_t bucket = storageCacheDebugBucket(c, key);
        assert(bucket < PROBE_RANGE);
        if (owner[bucket] == -1) {
            owner[bucket] = key;
        } else {
            *out_a = owner[bucket];
            *out_b = key;
            return;
        }
    }
    assert(0 && "no colliding pair found in search range");
}

static void test_hash_bucket_collision(void) {
    StorageCache* c = createCache(2);

    /* Probing storageCacheDebugBucket doesn't mutate the cache, so it's
     * safe to search for a colliding pair before inserting anything. */
    int k1, k2;
    find_colliding_pair(c, &k1, &k2);

    put(c, k1, 100); /* inserted first: becomes the LRU entry */
    put(c, k2, 200); /* same bucket: pushed to the head of that chain */
    assert(get(c, k1) == 100);
    assert(get(c, k2) == 200);
    /* k2's get() above just made k2 the MRU, so k1 is LRU again. */

    put(c, 999999, 300); /* cache full + new key: evicts k1 */
    /* k1 was not the head of its hash chain (k2 was inserted after it),
     * so this exercises hashRemove's `hprev != NIL` unlink branch. */
    assert(get(c, k1) == -1);
    assert(get(c, k2) == 200); /* chain must still be walkable */
    assert(get(c, 999999) == 300);

    freeCache(c);
}

/* Stress test: many keys exercise the hash chains and the LRU list at a
 * realistic size. Fills a capacity-N cache, verifies every value, then
 * re-reads three keys and inserts N-3 new ones -- LRU (not FIFO) means
 * exactly the untouched old keys are evicted and the re-read three survive. */
static void test_large_capacity(void) {
    enum { N = 1000 };
    StorageCache* c = createCache(N);

    for (int i = 0; i < N; i++) {
        put(c, i, i * 10);
    }
    for (int i = 0; i < N; i++) {
        assert(get(c, i) == i * 10);
    }

    /* Pull keys 0, 1, 2 back to the most-recently-used end. */
    assert(get(c, 0) == 0);
    assert(get(c, 1) == 10);
    assert(get(c, 2) == 20);

    /* Insert exactly N-3 new keys: only the untouched old keys (3..N-1)
     * should be evicted, in least-recently-used order. */
    for (int i = N; i < 2 * N - 3; i++) {
        put(c, i, i * 10);
    }

    assert(get(c, 0) == 0);   /* recently used -> survived */
    assert(get(c, 1) == 10);
    assert(get(c, 2) == 20);
    for (int i = 3; i < N; i++) {
        assert(get(c, i) == -1); /* evicted */
    }
    for (int i = N; i < 2 * N - 3; i++) {
        assert(get(c, i) == i * 10); /* newly inserted, present */
    }

    freeCache(c);
}

int main(void) {
    test_basic_get_put();
    test_eviction_order();
    test_readme_scenario();
    test_invalid_capacity();
    test_capacity_one();
    test_overwrite_then_evict();
    test_hash_bucket_collision();
    test_large_capacity();

    printf("All tests passed.\n");
    return 0;
}
