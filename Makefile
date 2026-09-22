CC = clang
CFLAGS = -O3 -Wall -Wextra -Iinclude -pthread -std=c11
LDFLAGS = -pthread

BIN_DIR = bin
SRC_SERVER = src/server/main.c src/server/net.c src/server/http.c src/server/threadpool.c src/server/stats.c
SRC_KV = src/kvstore/hashtable.c src/kvstore/murmur3.c src/kvstore/lru.c src/kvstore/ttl.c src/kvstore/aof.c
SRC_CLI = src/cli/cli_main.c
SRC_BENCH = src/bench/bench_main.c
SRC_TEST = tests/test_main.c

TARGET_DAEMON = $(BIN_DIR)/minisysd
TARGET_CLI = $(BIN_DIR)/minisys-cli
TARGET_BENCH = $(BIN_DIR)/minisys-bench
TARGET_TEST = $(BIN_DIR)/minisys-test

all: dirs $(TARGET_DAEMON) $(TARGET_CLI) $(TARGET_BENCH) $(TARGET_TEST)

dirs:
	@mkdir -p $(BIN_DIR)

$(TARGET_DAEMON): $(SRC_SERVER) $(SRC_KV)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CLI): $(SRC_CLI)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_BENCH): $(SRC_BENCH)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_TEST): $(SRC_TEST) $(SRC_KV) src/server/stats.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: $(TARGET_DAEMON)
	./$(TARGET_DAEMON) -p 8080 -t 8 -d minisys.aof -w web

test: $(TARGET_TEST)
	./$(TARGET_TEST)

bench: $(TARGET_BENCH)
	./$(TARGET_BENCH) -c 25 -n 10000

clean:
	rm -rf $(BIN_DIR) *.aof

.PHONY: all dirs run test bench clean
