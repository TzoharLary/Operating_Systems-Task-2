CC=gcc
CFLAGS=-Wall -g
TARGET=ttt

all: $(TARGET)

$(TARGET): tic_tac_toe.o
	$(CC) $(CFLAGS) -o $(TARGET) tic_tac_toe.o

tic_tac_toe.o: tic_tac_toe.c
	$(CC) $(CFLAGS) -c tic_tac_toe.c

clean:
	rm -f *.o $(TARGET)
