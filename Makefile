
icmpwatch: icmpwatch.c
	$(CC) -O2 -Wall -std=gnu99 icmpwatch.c -o icmpwatch
	strip icmpwatch

icmpwatch.dev: icmpwatch.c
	$(CC) -g -O2 -Wall -std=gnu99 -Wextra -pedantic -fsanitize=address icmpwatch.c -o icmpwatch.dev

.PHONY: build build-dev run install uninstall

build: icmpwatch

build-dev: icmpwatch.dev

clean:
	rm -f icmpwatch

run: icmpwatch
	./icmpwatch www.ibm.com www.apple.com www.google.com www.stolk.org www.canonical.com

install: icmpwatch
	install -d ${DESTDIR}/usr/bin
	install -m 755 icmpwatch ${DESTDIR}/usr/bin/

uninstall:
	rm -f ${DESTDIR}/usr/bin/icmpwatch

tarball:
	tar cvzf ../icmpwatch_1.0.orig.tar.gz Makefile icmpwatch.c compile_flags.txt console.png debian LICENSE README.md .clang-format

packageupload:
	debuild -S
	debsign ../icmpwatch_1.0-1_source.changes
	dput ppa:b-stolk/ppa ../icmpwatch_1.0-1_source.changes

