
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "git.h"

static char *slurp(int fd)
{
	char *s = 0;
	size_t len = 0;
	ssize_t r;
	for (;;) {
		s = realloc(s, len + 65537);
		if ((r = read(fd, s + len, 65536)) <= 0) break;
		len += r;
	}
	s[len] = 0;
	return s;
}

int git(char **out, const char *in, char **argv)
{
	int i[2], o[2], st;
	FILE *e = tmpfile();
	char *s, *t;
	pid_t pid;
	signal(SIGPIPE, SIG_IGN);
	if (!e || pipe(i) || pipe(o)) {
		if (out) *out = strdup("cannot run git");
		return -1;
	}
	if (!(pid = fork())) {
		dup2(i[0], 0);
		dup2(o[1], 1);
		dup2(fileno(e), 2);
		close(i[1]);
		close(o[0]);
		setenv("GIT_LITERAL_PATHSPECS", "1", 1);
		execvp(*argv, argv);
		_exit(127);
	}
	close(i[0]);
	close(o[1]);
	if (in) write(i[1], in, strlen(in));
	close(i[1]);
	s = slurp(o[0]);
	close(o[0]);
	waitpid(pid, &st, 0);
	if (st) {
		lseek(fileno(e), 0, SEEK_SET);
		t = slurp(fileno(e));
		if (*t) {
			free(s);
			s = t;
		} else free(t);
	}
	fclose(e);
	if (out) *out = s;
	else free(s);
	return st;
}

Entry *status(int *n)
{
	static char *s;
	static Entry *e;
	char *p;
	free(s);
	if (git(&s, 0, (char *[]){"git", "status", "--porcelain=v1", "-z", "-uall", 0})) *s = 0;
	for (*n = 0, p = s; *p; (*n)++) {
		e = realloc(e, (*n + 1) * sizeof *e);
		e[*n] = (Entry){p[0], p[1], p + 3, 0};
		p += strlen(p) + 1;
		if (*e[*n].path && strchr("RC", e[*n].x)) {
			e[*n].orig = p;
			p += strlen(p) + 1;
		}
	}
	return e;
}

char *diff(Entry *e, int cached)
{
	char *s, *u = !cached && e->y == '?' ? "--no-index" : 0;
	git(&s, 0, (char *[]){"git", "diff", "--no-color", "--no-ext-diff",
		cached ? "--cached" : u ? u : "-p", "--", u ? "/dev/null" : e->path, e->path, 0});
	return s;
}

char *patch(const char *d, int from, int to, int rev)
{
	char *r, *b, *q = 0;
	size_t n, m;
	int l = 0, h = -1, a = 0, c = 0, ob = 0, nb = 0, any = 0, chg = 0, drop = 0, ch;
	const char *p = d, *e;
	FILE *f = open_memstream(&r, &n), *g = 0;
	for (;; p = e, l++) {
		e = p + strcspn(p, "\n");
		e += !!*e;
		if (g && (!*p || !strncmp(p, "@@", 2))) {
			fclose(g);
			g = 0;
			if (chg) {
				fprintf(f, "@@ -%d,%d +%d,%d%s", a, ob, c, nb, q);
				fputs(b, f);
				any = 1;
			}
			free(q);
			free(b);
		}
		if (!*p) break;
		ch = *p;
		if (!strncmp(p, "@@", 2)) {
			h = l;
			a = strtol(p + 4, &q, 10);
			q = strchr(q, '+');
			c = strtol(q + 1, &q, 10);
			if (*q == ',') strtol(q + 1, &q, 10);
			q = strndup(q, e - q);
			ob = nb = chg = drop = 0;
			g = open_memstream(&b, &m);
		} else if (h < 0) fwrite(p, 1, e - p, f);
		else if (ch == '\\') {
			if (!drop) fwrite(p, 1, e - p, g);
		} else if ((ch == '+' || ch == '-') && !(to < 0 ? h == from : l >= from && l <= to)) {
			if (!(drop = ch == (rev ? '-' : '+'))) {
				fputc(' ', g);
				fwrite(p + 1, 1, e - p - 1, g);
				ob++;
				nb++;
			}
		} else {
			fwrite(p, 1, e - p, g);
			chg |= ch == '+' || ch == '-';
			ob += ch != '+';
			nb += ch != '-';
			drop = 0;
		}
	}
	fclose(f);
	if (any) return r;
	free(r);
	return 0;
}

int apply(char **out, const char *d, int from, int to, int rev)
{
	char *p = patch(d, from, to, rev);
	int r = p ? git(out, p, (char *[]){"git", "apply", "--cached", rev ? "-R" : "--cached", "-", 0}) : (*out = 0, 0);
	free(p);
	return r;
}

int files(char **out, int op, Entry **e, int n)
{
	static char *cmd[][4] = {
		{"git", "add", "-A", "--"},
		{"git", "reset", "-q", "--"},
		{"git", "checkout", "-q", "--"},
		{"git", "clean", "-fq", "--"}
	};
	char **a = malloc((2 * n + 5) * sizeof *a), **p = a + 4;
	int r;
	op += op == 2 && e[0]->y == '?';
	memcpy(a, cmd[op], sizeof *cmd);
	for (; n--; e++) {
		*p++ = (*e)->path;
		if (op == 1 && (*e)->orig) *p++ = (*e)->orig;
	}
	*p = 0;
	r = git(out, 0, a);
	free(a);
	return r;
}

int commit(char **out, const char *msg, int amend)
{
	return git(out, msg, (char *[]){"git", "commit", "-q", "-F", "-", amend ? "--amend" : 0, 0});
}

char *repo(const char *path)
{
	char *top;
	if ((path && chdir(path)) || git(&top, 0, (char *[]){"git", "rev-parse", "--show-toplevel", 0})) return 0;
	top[strcspn(top, "\n")] = 0;
	return chdir(top) ? 0 : top;
}

