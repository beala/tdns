CC = gcc
CFLAGS = -c -g -Wall -Wextra -pthread
LFLAGS = -Wall -Wextra -pthread

.PHONY: all clean

all: tdns

tdns: tdns.o queue.o util.o
	$(CC) $(LFLAGS) $^ -o $@

tdns.o: tdns.c tdns.h
	$(CC) $(CFLAGS) $<

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) $<

util.o: util.c util.h
	$(CC) $(CFLAGS) $<

clean:
	rm -f tdns
	rm -f *.o
	rm -f *~
	rm -f results.txt
