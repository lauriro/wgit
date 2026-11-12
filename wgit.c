
#include <stdio.h>
#include <string.h>

int cmd_commit(const char *path);
int cmd_log(const char *path);

static const struct {
	const char *name;
	int (*fn)(const char *path);
} cmds[] = {
	{"commit", cmd_commit},
	{"log", cmd_log},
};

int main(int argc, char **argv)
{
	int i, n = 0, k = 0;
	for (i = 0; argc > 1 && *argv[1] && i < sizeof cmds / sizeof *cmds; i++)
		if (!strncmp(cmds[i].name, argv[1], strlen(argv[1]))) n++, k = i;
	if (n == 1) return cmds[k].fn(argv[2]);
	fputs("usage: wgit c[ommit] [path]\n       wgit l[og] [path]\n", stderr);
	return argc > 1;
}

