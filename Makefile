CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude

SRC := src/storage_cache.c
HDR := include/storage_cache.h
TEST_HDR := $(HDR) include/storage_cache_test_support.h
BUILD := build

.PHONY: all demo test clean

all: demo test

demo: $(BUILD)/demo
	./$(BUILD)/demo

test: $(BUILD)/test_storage_cache
	./$(BUILD)/test_storage_cache

$(BUILD)/demo: $(SRC) src/main.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) $(SRC) src/main.c -o $@

# -DSTORAGE_CACHE_TEST_SUPPORT compiles in storageCacheDebugBucket(),
# a test-only accessor used to construct deterministic hash collisions;
# it's absent from the demo build so production keeps the exact 4-function API.
$(BUILD)/test_storage_cache: $(SRC) tests/test_storage_cache.c $(TEST_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -DSTORAGE_CACHE_TEST_SUPPORT $(SRC) tests/test_storage_cache.c -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
