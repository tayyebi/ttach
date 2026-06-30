CC ?= gcc
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c99 -D_DEFAULT_SOURCE
LDFLAGS ?=

SRC = src/main.c src/server.c src/client.c src/pty.c \
      src/socket.c src/relay.c src/signal.c
OBJ = $(SRC:.c=.o)
TARGET = ttach
TEST_TARGET = test/test_ttach

.PHONY: all clean test

all: $(TARGET)

test: $(TARGET) $(TEST_TARGET)
	./$(TEST_TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): test/test_ttach.c
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_TARGET)
