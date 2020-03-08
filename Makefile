CC=clang
LD=clang
CFLAGS=-g -Wall -Wextra -pedantic
LDFLAGS=-g

.PHONY: all clean

all: bmaild

clean:
	rm -f *.o
	rm -f bmaild

bmaild: bmaild.o recv.o mbox.o smtp.o util.o
bmaild.o: util.h mbox.h
recv.o: util.h smtp.h mbox.h
mbox.o: util.h smtp.h mbox.h
smtp.o: smtp.h
util.o: util.h
