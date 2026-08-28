#include <stdio.h>

#include "storage_cache.h"

int main(void) {
    StorageCache* store = createCache(4);
    if (store == NULL) {
        fprintf(stderr, "createCache failed\n");
        return 1;
    }

    put(store, 1, 100); /* 최신순: 1 */
    put(store, 2, 200); /* 최신순: 2 -> 1 */
    put(store, 3, 300); /* 최신순: 3 -> 2 -> 1 */
    put(store, 4, 400); /* 최신순: 4 -> 3 -> 2 -> 1 (저장소 가득 참) */

    printf("get(1) = %d (expected 100)\n", get(store, 1));
    /* 최신순: 1 -> 4 -> 3 -> 2 */

    put(store, 5, 500); /* 2 제거, 최신순: 5 -> 1 -> 4 -> 3 */

    printf("get(2) = %d (expected -1)\n", get(store, 2));

    put(store, 6, 600); /* 3 제거, 최신순: 6 -> 5 -> 1 -> 4 */

    printf("get(3) = %d (expected -1)\n", get(store, 3));
    printf("get(1) = %d (expected 100)\n", get(store, 1));
    /* 최신순: 1 -> 6 -> 5 -> 4 */
    printf("get(5) = %d (expected 500)\n", get(store, 5));
    /* 최신순: 5 -> 1 -> 6 -> 4 */

    freeCache(store);
    return 0;
}
