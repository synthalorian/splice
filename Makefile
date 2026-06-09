CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lzstd -lxxhash

SRC = src/main.c src/store.c src/delta.c src/cli.c
OBJ = $(SRC:.c=.o)
TARGET = splice

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

test:
	@echo "Tests not yet implemented"
