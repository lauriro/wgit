
CFLAGS ?= -Os -Wall -fno-asynchronous-unwind-tables -fno-plt
LDFLAGS ?= -s -Wl,-z,noseparate-code,-z,pack-relative-relocs,--build-id=none
GTK = `pkg-config --cflags --libs gtk+-3.0`

wgit: wgit.c commit.c log.c stat.c diff-view.c diff-view.h git.c git.h ui.c ui.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ wgit.c commit.c log.c stat.c diff-view.c git.c ui.c $(GTK)

wgit-test: test.c git.c git.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ test.c git.c

test: wgit-test
	sh test.sh

test-gtk:
	$(CC) $(CFLAGS) -o /tmp/wgit-test-gtk test-gtk.c commit.c log.c diff-view.c git.c ui.c $(GTK)
	G_DEBUG=fatal-warnings xvfb-run -a /tmp/wgit-test-gtk

clean:
	rm -f wgit wgit-test

.PHONY: test clean

