CC=clang
LD=clang
CFLAGS=-g -Wall -Wextra -pedantic
LDFLAGS=-g

.PHONY: all clean

all: bmaild

clean:
	rm -f *.o
	rm -f bmaild

bmaild: bmaild.o server.o recv.o smtp.o util.o
bmaild.o: arg.h util.h conf.h
server.o: util.h
recv.o: conf.h util.h smtp.h
smtp.o: util.h smtp.h
util.o: util.h
