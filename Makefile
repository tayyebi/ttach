CC ?= gcc
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c99 -D_DEFAULT_SOURCE
LDFLAGS ?=

SRC = src/main.c src/server.c src/client.c src/pty.c \
      src/socket.c src/relay.c src/signal.c
OBJ = $(SRC:.c=.o)
TARGET = ttach

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)
