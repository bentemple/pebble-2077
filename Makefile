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

# Compile and run tests
test: $(TEST_BIN)
	./$(TEST_BIN)

# Compile tests
$(TEST_BIN): $(TEST_DIR)/uptime_test.c $(SRC_DIR)/uptime.c $(SRC_DIR)/uptime.h
	gcc -I$(SRC_DIR) -o $@ $(TEST_DIR)/uptime_test.c $(SRC_DIR)/uptime.c

# Compile tests with debug output
test-debug: $(TEST_DIR)/uptime_test.c $(SRC_DIR)/uptime.c $(SRC_DIR)/uptime.h
	gcc -I$(SRC_DIR) -DUPTIME_DEBUG -o $(TEST_BIN) $(TEST_DIR)/uptime_test.c $(SRC_DIR)/uptime.c
	./$(TEST_BIN)

# Clean test artifacts
test-clean:
	rm -f $(TEST_BIN)

# Clean everything
distclean: clean test-clean
