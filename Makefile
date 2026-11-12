
CFLAGS ?= -Os -Wall
LDFLAGS ?= -s
GTK = `pkg-config --cflags --libs gtk+-3.0`

wgit: wgit.c commit.c diff-view.c diff-view.h git.c git.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ wgit.c commit.c diff-view.c git.c $(GTK)

wgit-test: test.c git.c git.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ test.c git.c

test: wgit-test
	sh test.sh

clean:
	rm -f wgit wgit-test

.PHONY: test clean

