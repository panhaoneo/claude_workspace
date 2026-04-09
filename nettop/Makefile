CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -g -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lncurses -lpanel -lpthread

SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)
TARGET  = nettop

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean
