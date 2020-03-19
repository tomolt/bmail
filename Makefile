# See LICENSE file for copyright and license details.

.POSIX:

include config.mk

.PHONY: all clean install uninstall

all: bmaild bmail_recv bmail_send

bmaild: bmaild.o util.o
	$(LD) $(LDFLAGS) $^$> -o $@

bmail_recv: bmail_recv.o mbox.o smtp.o conf.o conn.o util.o
	$(LD) $(LDFLAGS) $(TLSLIBS) $^$> -o $@

bmail_send: bmail_send.o conf.o conn.o util.o
	$(LD) $(LDFLAGS) $(TLSLIBS) $^$> -o $@

bmaild.o: util.h
bmail_recv.o: conf.h conn.h mbox.h smtp.h util.h
conf.o: conf.h util.h
conn.o: conf.h conn.h util.h
mbox.o: mbox.h util.h
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

