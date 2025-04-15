# Compilador y banderas
CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 \
          -D_POSIX_C_SOURCE=200809L \
          -D_XOPEN_SOURCE=700 \
          -D_DEFAULT_SOURCE \
          -fno-common

# Nombre del ejecutable
TARGET = desafio1
SRC = desafio1.c

# Reglas
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean