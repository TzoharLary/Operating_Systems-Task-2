CC=gcc
CFLAGS=-Wall -g
TARGETS=mync ttt

all: $(TARGETS)

mync: mync.o
	$(CC) $(CFLAGS) -o mync mync.o

ttt: tic_tac_toe.o
	$(CC) $(CFLAGS) -o ttt tic_tac_toe.o

mync.o: mync.c
	$(CC) $(CFLAGS) -c mync.c

tic_tac_toe.o: tic_tac_toe.c
	$(CC) $(CFLAGS) -c tic_tac_toe.c

clean:
	rm -f *.o $(TARGETS)
