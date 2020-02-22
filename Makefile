CC=clang
LD=clang
CFLAGS=-Wall -Wextra -pedantic
LDFLAGS=-g

.PHONY: all clean

all: bmaild

clean:
	rm -f *.o
	rm -f bmaild

bmaild: bmaild.o server.o smtp.o util.o
server.o: smtp.h util.h conf.h
