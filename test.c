
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "git.h"

int main(int argc, char **argv)
{
	static const char *op[] = {"add", "reset", "revert", "ignore", "ignore-sorted", "stage", "unstage", "discard"};
	int i, j, n, k = 0, r = 0;
	char *o = 0;
	Entry *e, **f = calloc(argc, sizeof *f);
	if (argc > 1 && argc < 4 && !strcmp(argv[1], "log")) {
		Row *w = history(&n, argc > 2 ? atoi(argv[2]) : 1 << 30);
		for (i = 0; i < n; i++) printf("%s\t%s\t%s\n", w[i].graph, w[i].hash ? w[i].refs : "", w[i].hash ? w[i].subject : "");
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "notes")) {
		Row *w = history(&n, 1 << 30);
		for (i = 0; i < n; i++) if (w[i].note) puts(w[i].subject);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "joins")) {
		Row *w = history(&n, 1 << 30);
		for (i = 0; i < n; i++) if (w[i].hash) {
			k = strchr(w[i].graph, '*') - w[i].graph;
			printf("%s %c%c\n", w[i].subject, joins(i ? w[i - 1].graph : 0, k, 0) ? 'u' : '-', joins(i + 1 < n ? w[i + 1].graph : 0, k, 1) ? 'd' : '-');
		}
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "amend")) base = parent(), argv++, argc--;
	if (argc > 1 && !strncmp(argv[1], "-U", 2)) context = atoi(argv[1] + 2), argv++, argc--;
	status();
	e = ent;
	n = nent;
	if (argc == 3 && !strcmp(argv[1], "diffs")) return fputs(diffs(atoi(argv[2]), e, n), stdout), 0;
	for (i = 0; argc < 3 && i < n; i++) printf("%c%c %s%s%s\n", e[i].x, e[i].y, e[i].path, e[i].orig ? " < " : "", e[i].orig ? e[i].orig : "");
	if (argc < 3) return 0;
	if (*argv[1] == 'c') r = commit(&o, argv[2], !!base);
	else {
		while (k < 8 && strcmp(argv[1], op[k])) k++;
		if (k > 7) return 2;
		for (j = 2; j < (k < 5 ? argc : *argv[2] == ':' ? 2 : 3); j++) {
			for (i = 0; i < n && strcmp(e[i].path, argv[j]); i++);
			if (i == n) return 2;
			f[j - 2] = e + i;
		}
		r = k < 3 ? files(&o, k, f, argc - 2)
			: k < 5 ? ignore(&o, patterns(f, argc - 2), k > 3)
			: apply(&o, *argv[2] == ':' ? diffs(argv[2][1] - '0', e, n) : diff(*f, k == 6), atoi(argv[3]), argc > 4 ? atoi(argv[4]) : -1, k - 5);
	}
	if (o) fputs(o, stderr);
	return !!r;
}

