CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L

all: desafio1

desafio1: desafio1.c
	$(CC) $(CFLAGS) -o desafio1 desafio1.c

clean:
	rm -f desafio1

.PHONY: all clean