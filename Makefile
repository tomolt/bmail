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
bmaild.o: arg.h util.h conf.h
server.o: smtp.h util.h conf.h
smtp.o: smtp.h util.h
util.o: util.h
