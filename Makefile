CC := gcc

# Shared warning/standard flags; per-target optimization is appended below.
BASE_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude

CFLAGS := $(BASE_CFLAGS) -O2

# AddressSanitizer + UndefinedBehaviorSanitizer build. -O1 with frame
# pointers keeps stack traces readable; -fno-sanitize-recover=all makes the
# first violation abort with a non-zero exit so `make test-asan` is a gate.
SAN_CFLAGS := $(BASE_CFLAGS) -O1 -g \
              -fsanitize=address,undefined -fno-sanitize-recover=all \
              -fno-omit-frame-pointer

SRC := src/storage_cache.c
HDR := include/storage_cache.h
TEST_HDR := $(HDR) include/storage_cache_test_support.h
BUILD := build

.PHONY: all demo test test-asan clean

all: demo test

demo: $(BUILD)/demo
	./$(BUILD)/demo

test: $(BUILD)/test_storage_cache
	./$(BUILD)/test_storage_cache

# Runs the unit tests under the sanitizers. Opt-in (not part of `all`)
# since it needs a sanitizer-capable toolchain -- Linux/macOS gcc or
# clang; MinGW on Windows has limited/no support.
test-asan: $(BUILD)/test_storage_cache_asan
	./$(BUILD)/test_storage_cache_asan

$(BUILD)/demo: $(SRC) src/main.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) $(SRC) src/main.c -o $@

# -DSTORAGE_CACHE_TEST_SUPPORT compiles in storageCacheDebugBucket(),
# a test-only accessor used to construct deterministic hash collisions;
# it's absent from the demo build so production keeps the exact 4-function API.
$(BUILD)/test_storage_cache: $(SRC) tests/test_storage_cache.c $(TEST_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -DSTORAGE_CACHE_TEST_SUPPORT $(SRC) tests/test_storage_cache.c -o $@

$(BUILD)/test_storage_cache_asan: $(SRC) tests/test_storage_cache.c $(TEST_HDR) | $(BUILD)
	$(CC) $(SAN_CFLAGS) -DSTORAGE_CACHE_TEST_SUPPORT $(SRC) tests/test_storage_cache.c -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
