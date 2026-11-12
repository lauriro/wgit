
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "git.h"

int main(int argc, char **argv)
{
	int i, n, r = 0, rev = argc > 1 && strchr("ru", *argv[1]);
	char *o = 0;
	Entry *e = status(&n), *f = 0;
	for (i = 0; i < n; i++) {
		if (argc < 3) printf("%c%c %s%s%s\n", e[i].x, e[i].y, e[i].path, e[i].orig ? " < " : "", e[i].orig ? e[i].orig : "");
		else if (!strcmp(e[i].path, argv[2])) f = e + i;
	}
	if (argc < 3) return 0;
	if (*argv[1] == 'c') r = commit(&o, argv[2], 0);
	else if (!f) return 2;
	else if (argc < 4) r = stage(&o, f, rev);
	else r = apply(&o, diff(f, rev), atoi(argv[3]), argc > 4 ? atoi(argv[4]) : -1, rev);
	if (o) fputs(o, stderr);
	return !!r;
}

