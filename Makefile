# Duck Soup - a cartoon screensaver of the sight gags from Duck Soup (1933)
VERSION  := 1.0.0
PREFIX   ?= /usr
SYSCONFDIR ?= /etc
DESTDIR  ?=
CC       ?= cc
# gnu11 keeps the binary free of very recent glibc symbols, so packages built on a
# new distribution still run on older ones
CFLAGS   ?= -O2 -g
override CFLAGS += -std=gnu11 -Wall -Isrc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

MINGW    ?= x86_64-w64-mingw32
MINGW_ROOT ?= /usr/$(MINGW)/sys-root/mingw

all: build/duck-soup

# the pictures in assets/art are built into the program, so it must be compiled from here
build/duck-soup: src/ducksoup.c src/stb_image.h src/stb_vorbis.c $(wildcard assets/art/*)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -o $@ src/ducksoup.c $(LDFLAGS) $(SDL_LIBS) -lm

check: build/duck-soup
	SDL_VIDEODRIVER=dummy ./build/duck-soup --snapshot build/check.bmp --time 120 --mute --size 640x400 && echo "ran a full round"

install: all
	install -Dm755 build/duck-soup $(DESTDIR)$(PREFIX)/bin/duck-soup
	install -Dm755 packaging/linux/duck-soup-screensaver $(DESTDIR)$(PREFIX)/bin/duck-soup-screensaver
	install -Dm644 packaging/linux/duck-soup.desktop $(DESTDIR)$(PREFIX)/share/applications/duck-soup.desktop
	install -Dm644 packaging/linux/duck-soup-saver.desktop $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/duck-soup.desktop
	install -Dm644 packaging/linux/duck-soup-autostart.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/duck-soup-screensaver.desktop
	install -Dm644 packaging/duck-soup.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/duck-soup.png
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/duck-soup/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/duck-soup/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/duck-soup $(DESTDIR)$(PREFIX)/bin/duck-soup-screensaver
	rm -f $(DESTDIR)$(PREFIX)/share/applications/duck-soup.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/duck-soup-screensaver.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/duck-soup.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/duck-soup.png
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/duck-soup

# ---- packages (written to dist/) ----
ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

deb: all
	rm -rf build/deb && mkdir -p build/deb/DEBIAN dist
	$(MAKE) install DESTDIR=build/deb PREFIX=/usr
	strip build/deb/usr/bin/duck-soup
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(ARCH_DEB)/' \
	    -e "s/@SIZE@/$$(du -sk build/deb/usr | cut -f1)/" packaging/linux/control.in > build/deb/DEBIAN/control
	echo /etc/xdg/autostart/duck-soup-screensaver.desktop > build/deb/DEBIAN/conffiles
	dpkg-deb --root-owner-group --build build/deb dist/duck-soup_$(VERSION)_$(ARCH_DEB).deb

rpm:
	mkdir -p dist build/rpm/SOURCES
	git archive --format=tar.gz --prefix=duck-soup-$(VERSION)/ -o build/rpm/SOURCES/duck-soup-$(VERSION).tar.gz HEAD
	rpmbuild -bb $(RPMFLAGS) --define "_topdir $(CURDIR)/build/rpm" --define "version $(VERSION)" packaging/linux/duck-soup.spec
	cp build/rpm/RPMS/*/duck-soup-$(VERSION)-*.rpm dist/

# Windows: the screensaver itself, and a small DuckSoup.scr that goes in the
# Windows system folder (so it is listed with the other screensavers) and starts it
windows:
	@mkdir -p build/win
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Isrc -I$(MINGW_ROOT)/include/SDL2 -Dmain=SDL_main \
	    -o build/win/duck-soup.exe src/ducksoup.c \
	    -L$(MINGW_ROOT)/lib -lmingw32 -lSDL2main -lSDL2 -lm -mwindows -static-libgcc
	$(MINGW)-windres packaging/windows/stub.rc -O coff -o build/win/stub.res
	$(MINGW)-gcc -O2 -Wall -o build/win/DuckSoup.scr packaging/windows/stub.c build/win/stub.res -mwindows -static
	$(MINGW)-strip build/win/duck-soup.exe build/win/DuckSoup.scr
	cp $(MINGW_ROOT)/bin/SDL2.dll build/win/
	[ ! -f $(MINGW_ROOT)/bin/SDL3.dll ] || cp $(MINGW_ROOT)/bin/SDL3.dll build/win/

installer: windows
	mkdir -p dist
	makensis -V2 -DVERSION=$(VERSION) packaging/windows/duck-soup.nsi

dist: deb rpm installer

clean:
	rm -rf build dist

.PHONY: all check install uninstall deb rpm windows installer dist clean
