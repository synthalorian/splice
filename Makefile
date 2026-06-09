CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lzstd -lxxhash

SRC = src/main.c src/store.c src/delta.c src/tree.c src/commit.c src/refs.c
OBJ = $(SRC:.c=.o)
TARGET = splice

TEST_STORE_SRC = tests/test_store.c src/store.c
TEST_STORE_OBJ = $(TEST_STORE_SRC:.c=.o)
TEST_STORE_TARGET = tests/test_store

TEST_DELTA_SRC = tests/test_delta.c src/store.c src/delta.c
TEST_DELTA_OBJ = $(TEST_DELTA_SRC:.c=.o)
TEST_DELTA_TARGET = tests/test_delta

TEST_TREE_SRC = tests/test_tree.c src/store.c src/tree.c
TEST_TREE_OBJ = $(TEST_TREE_SRC:.c=.o)
TEST_TREE_TARGET = tests/test_tree

TEST_COMMIT_SRC = tests/test_commit.c src/store.c src/tree.c src/commit.c src/refs.c
TEST_COMMIT_OBJ = $(TEST_COMMIT_SRC:.c=.o)
TEST_COMMIT_TARGET = tests/test_commit

TEST_REFS_SRC = tests/test_refs.c src/store.c src/refs.c
TEST_REFS_OBJ = $(TEST_REFS_SRC:.c=.o)
TEST_REFS_TARGET = tests/test_refs

TEST_TARGETS = $(TEST_STORE_TARGET) $(TEST_DELTA_TARGET) $(TEST_TREE_TARGET) $(TEST_COMMIT_TARGET) $(TEST_REFS_TARGET)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(TEST_STORE_TARGET): $(TEST_STORE_OBJ)
	$(CC) $(TEST_STORE_OBJ) -o $@ $(LDFLAGS)

$(TEST_DELTA_TARGET): $(TEST_DELTA_OBJ)
	$(CC) $(TEST_DELTA_OBJ) -o $@ $(LDFLAGS)

$(TEST_TREE_TARGET): $(TEST_TREE_OBJ)
	$(CC) $(TEST_TREE_OBJ) -o $@ $(LDFLAGS)

$(TEST_COMMIT_TARGET): $(TEST_COMMIT_OBJ)
	$(CC) $(TEST_COMMIT_OBJ) -o $@ $(LDFLAGS)

$(TEST_REFS_TARGET): $(TEST_REFS_OBJ)
	$(CC) $(TEST_REFS_OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGETS)
	./$(TEST_STORE_TARGET)
	./$(TEST_DELTA_TARGET)
	./$(TEST_TREE_TARGET)
	./$(TEST_COMMIT_TARGET)
	./$(TEST_REFS_TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_STORE_OBJ) $(TEST_DELTA_OBJ) $(TEST_TREE_OBJ) $(TEST_COMMIT_OBJ) $(TEST_REFS_OBJ) $(TEST_TARGETS)
