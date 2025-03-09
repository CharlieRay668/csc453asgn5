CC = gcc
CFLAGS = -Wall -Wextra -g -O2 


.PHONY: all clean test


all: minls minget

minls: minls.o minutil.o
	$(CC) $(CFLAGS) -o minls minls.o minutil.o

minget: minget.o minutil.o
	$(CC) $(CFLAGS) -o minget minget.o minutil.o

minls.o: minls.c minutil.h
	$(CC) $(CFLAGS) -c minls.c

minget.o: minget.c minutil.h
	$(CC) $(CFLAGS) -c minget.c

minutil.o: minutil.c minutil.h
	$(CC) $(CFLAGS) -c minutil.c

clean:
	rm -rf *.o minget minls

test:
	~pn-cs453/demos/tryAsgn5 > test_out.txt