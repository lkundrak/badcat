VERSION = 1.0

PKG_CONFIG = pkg-config
INSTALL = install

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man
UNITDIR = $(PREFIX)/lib/systemd/system

libudev_CFLAGS = $(shell $(PKG_CONFIG) --cflags libudev)
libudev_LIBS = $(shell $(PKG_CONFIG) --libs libudev)

CFLAGS = -O2 -Wall

override CFLAGS += $(libudev_CFLAGS)
override LDFLAGS += $(libudev_LIBS)

all: badcat badcat.8

badcat.8: badcat.pod
	pod2man --center 'System management commands' --release $(VERSION) $< >$@

badcat.pdf: badcat.8
	groff -Tpdf -mman $< >$@

install:
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(UNITDIR) $(DESTDIR)$(MANDIR)/man8
	$(INSTALL) badcat $(DESTDIR)$(BINDIR)/
	$(INSTALL) -pm644 badcat.service $(DESTDIR)$(UNITDIR)/
	$(INSTALL) -m644 badcat.8 $(DESTDIR)$(MANDIR)/man8/
	@echo
	@echo To use the service, run the following:
	@echo
	@echo systemctl daemon-reload
	@echo systemctl enable badcat.service
	@echo systemctl start badcat.service
	@echo

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/badcat
	rm -f $(DESTDIR)$(UNITDIR)/badcat.service
	rm -f $(DESTDIR)$(MANDIR)/man8/badcat.8

clean:
	rm -f badcat badcat.8 *.o
