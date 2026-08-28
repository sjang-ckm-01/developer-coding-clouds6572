#ifndef STORAGE_CACHE_TEST_SUPPORT_H
#define STORAGE_CACHE_TEST_SUPPORT_H

/*
 * Test-only accessor into StorageCache internals. Not part of the
 * public API (see storage_cache.h) -- only tests should include this
 * header, and only builds compiled with STORAGE_CACHE_TEST_SUPPORT
 * defined expose the corresponding function from storage_cache.c.
 */

#include <stddef.h>

#include "storage_cache.h"

/* Returns the bucket `key` would hash into for this exact cache
 * instance (its real, per-instance seed included), so a test can
 * deterministically find keys that collide in production instead of
 * guessing with a hand-copied, unseeded reimplementation of hashKey. */
size_t storageCacheDebugBucket(StorageCache* cache, int key);

#endif /* STORAGE_CACHE_TEST_SUPPORT_H */
