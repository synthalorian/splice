CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lzstd -lxxhash

SRC = src/main.c src/store.c src/delta.c
OBJ = $(SRC:.c=.o)
TARGET = splice

TEST_STORE_SRC = tests/test_store.c src/store.c
TEST_STORE_OBJ = $(TEST_STORE_SRC:.c=.o)
TEST_STORE_TARGET = tests/test_store

TEST_DELTA_SRC = tests/test_delta.c src/store.c src/delta.c
TEST_DELTA_OBJ = $(TEST_DELTA_SRC:.c=.o)
TEST_DELTA_TARGET = tests/test_delta

TEST_TARGETS = $(TEST_STORE_TARGET) $(TEST_DELTA_TARGET)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(TEST_STORE_TARGET): $(TEST_STORE_OBJ)
	$(CC) $(TEST_STORE_OBJ) -o $@ $(LDFLAGS)

$(TEST_DELTA_TARGET): $(TEST_DELTA_OBJ)
	$(CC) $(TEST_DELTA_OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGETS)
	./$(TEST_STORE_TARGET)
	./$(TEST_DELTA_TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_STORE_OBJ) $(TEST_DELTA_OBJ) $(TEST_TARGETS)
