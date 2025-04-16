CC=gcc
CFLAGS=-Wall -g
TARGET=desafio1

all: $(TARGET)

$(TARGET): desafio1.c
	$(CC) $(CFLAGS) -o $(TARGET) desafio1.c

clean:
	rm -f $(TARGET)