.POSIX:

include config.mk

.PHONY: all clean install uninstall

all: bmaild bmail_recv

bmaild: bmaild.o util.o
bmaild.o: util.h
bmail_recv: bmail_recv.o mbox.o smtp.o util.o
bmail_recv.o: util.h smtp.h mbox.h
mbox.o: util.h smtp.h mbox.h
smtp.o: smtp.h
util.o: util.h

clean:
	rm -f *.o
	rm -f bmaild
	rm -f bmail_recv

install: all
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	cp -f bmail "$(DESTDIR)$(PREFIX)/bin"
	chmod 755 "$(DESTDIR)$(PREFIX)/bin/bmail"
	cp -f bmaild "$(DESTDIR)$(PREFIX)/bin"
	chmod 755 "$(DESTDIR)$(PREFIX)/bin/bmaild"
	cp -f bmail_recv "$(DESTDIR)$(PREFIX)/bin"
	chmod 755 "$(DESTDIR)$(PREFIX)/bin/bmail_recv"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/bmail"
	rm -f "$(DESTDIR)$(PREFIX)/bin/bmaild"
	rm -f "$(DESTDIR)$(PREFIX)/bin/bmail_recv"

