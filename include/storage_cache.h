#ifndef STORAGE_CACHE_H
#define STORAGE_CACHE_H

/*
 * StorageCache: fixed-capacity LRU (Least Recently Used) cache.
 *
 * get/put both run in average O(1) time. The implementation combines:
 *   - a fixed-size node pool (array), so no per-call malloc/free,
 *   - a singly-linked hash table (separate chaining) for key -> node lookup,
 *   - a doubly-linked list threaded through the same node pool for recency
 *     order (head = most recently used, tail = least recently used).
 * No standard/library map, list, or cache/LRU class is used as a wrapper;
 * only raw arrays and indices are used to build the structure by hand.
 */

typedef struct StorageCache StorageCache;

/* Creates a cache with the given positive capacity. Returns NULL on
 * invalid capacity (<= 0) or allocation failure -- always check the
 * result before calling get/put with it. */
StorageCache* createCache(int capacity);

/* Returns the value for key, or -1 if key is not present -- including
 * when `cache` is NULL, which is treated the same as an empty cache
 * (consistent with freeCache's NULL-tolerant convention below).
 * A successful lookup marks key as most recently used. */
int get(StorageCache* cache, int key);

/* Inserts or updates key with value, marking it as most recently used.
 * If the key is new and the cache is full, the least recently used
 * entry is evicted first. A NULL `cache` is a no-op. */
void put(StorageCache* cache, int key, int value);

/* Releases all memory owned by the cache. A NULL cache is a no-op
 * (mirrors free()'s convention). */
void freeCache(StorageCache* cache);

#endif /* STORAGE_CACHE_H */
