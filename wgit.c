
#include <stdio.h>
#include <string.h>

int app(const char *path, int view);

static const char *cmds[] = {"commit", "log"};

int main(int argc, char **argv)
{
	int i, n = 0, k = 0;
	if (argc < 2) return app(0, -1);
	for (i = 0; *argv[1] && i < 2; i++) if (!strncmp(cmds[i], argv[1], strlen(argv[1]))) n++, k = i;
	if (n == 1) return app(argv[2], k);
	fputs("usage: wgit [c[ommit] [path] | l[og] [path]]\n", stderr);
	return 1;
}

