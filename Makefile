# Pebble 2077 Makefile

.PHONY: all build clean test test-clean install

# Default target
all: build

# Build the Pebble app
build:
	pebble build

# Clean Pebble build artifacts
clean:
	pebble clean

# Install to emulator or watch
install:
	pebble install

# ============================================================
# Unit Tests
# ============================================================

TEST_DIR = test
SRC_DIR = src/c
TEST_BIN = $(TEST_DIR)/uptime_test.o
REFRESH_TEST_BIN = $(TEST_DIR)/refresh_test.o
CFLAGS_TEST = -Wall -Wextra -I$(SRC_DIR)

# Compile and run all tests
test: $(TEST_BIN) $(REFRESH_TEST_BIN)
	./$(TEST_BIN)
	./$(REFRESH_TEST_BIN)

# Compile uptime tests
$(TEST_BIN): $(TEST_DIR)/uptime_test.c $(SRC_DIR)/uptime.c $(SRC_DIR)/uptime.h
	gcc $(CFLAGS_TEST) -o $@ $(TEST_DIR)/uptime_test.c $(SRC_DIR)/uptime.c

# Compile refresh scheduling tests
$(REFRESH_TEST_BIN): $(TEST_DIR)/refresh_test.c $(SRC_DIR)/refresh.c $(SRC_DIR)/refresh.h
	gcc $(CFLAGS_TEST) -o $@ $(TEST_DIR)/refresh_test.c $(SRC_DIR)/refresh.c

# Compile tests with debug output
test-debug: $(TEST_DIR)/uptime_test.c $(SRC_DIR)/uptime.c $(SRC_DIR)/uptime.h
	gcc $(CFLAGS_TEST) -DUPTIME_DEBUG -o $(TEST_BIN) $(TEST_DIR)/uptime_test.c $(SRC_DIR)/uptime.c
	./$(TEST_BIN)

# Clean test artifacts
test-clean:
	rm -f $(TEST_BIN) $(REFRESH_TEST_BIN)

# Clean everything
distclean: clean test-clean
