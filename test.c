
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "git.h"

int main(int argc, char **argv)
{
	static const char *op[] = {"add", "reset", "revert", "ignore", "ignore-sorted"};
	int i, j, n, k = 0, r = 0, rev;
	char *o = 0;
	Entry *e, **f = calloc(argc, sizeof *f);
	if (argc > 1 && !strcmp(argv[1], "amend")) base = parent(), argv++, argc--;
	rev = argc > 1 && *argv[1] == 'u';
	e = status(&n);
	for (i = 0; argc < 3 && i < n; i++) printf("%c%c %s%s%s\n", e[i].x, e[i].y, e[i].path, e[i].orig ? " < " : "", e[i].orig ? e[i].orig : "");
	if (argc < 3) return 0;
	if (*argv[1] == 'c') r = commit(&o, argv[2], !!base);
	else {
		while (k < 5 && strcmp(argv[1], op[k])) k++;
		for (j = 2; j < (k < 5 ? argc : 3); j++) {
			for (i = 0; i < n && strcmp(e[i].path, argv[j]); i++);
			if (i == n) return 2;
			f[j - 2] = e + i;
		}
		r = k < 3 ? files(&o, k, f, argc - 2) : k < 5 ? ignore(&o, patterns(f, argc - 2), k > 3) : apply(&o, diff(*f, rev), atoi(argv[3]), argc > 4 ? atoi(argv[4]) : -1, rev);
	}
	if (o) fputs(o, stderr);
	return !!r;
}

