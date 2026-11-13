
#define _GNU_SOURCE
#include <fcntl.h>
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

char *base;
int context = 3;

char *parent(void)
{
	char *s;
	if (git(&s, 0, (char *[]){"git", "rev-parse", "-q", "--verify", "HEAD^", 0})) {
		free(s);
		git(&s, 0, (char *[]){"git", "hash-object", "-t", "tree", "/dev/null", 0});
	}
	s[strcspn(s, "\n")] = 0;
	return s;
}

static int cmp(const void *a, const void *b)
{
	return strcmp(((const Entry *)a)->path, ((const Entry *)b)->path);
}

Entry *status(int *n)
{
	static char *s, *d;
	static Entry *e;
	char *p, *q, *o;
	int i;
	free(s);
	free(d);
	d = 0;
	if (git(&s, 0, (char *[]){"git", "status", "--porcelain=v1", "-z", "-uall", 0})) *s = 0;
	for (*n = 0, p = s; *p; (*n)++) {
		e = realloc(e, (*n + 1) * sizeof *e);
		e[*n] = (Entry){p[0], p[1], p + 3, 0};
		p += strlen(p) + 1;
		if (*e[*n].path && strchr("RC", e[*n].x)) {
			e[*n].orig = p;
			p += strlen(p) + 1;
		}
		if (base && e[*n].x != '?') {
			e[*n].x = ' ';
			e[*n].orig = 0;
			if (e[*n].y == ' ') (*n)--;
		}
	}
	if (base && !git(&d, 0, (char *[]){"git", "diff", "--cached", "--name-status", "-z", base, 0})) {
		for (p = d; *p; p = q + strlen(q) + 1) {
			q = p + strlen(p) + 1;
			o = strchr("RC", *p) ? q : 0;
			if (o) q += strlen(q) + 1;
			for (i = 0; i < *n && strcmp(e[i].path, q); i++);
			if (i == *n) {
				e = realloc(e, ++*n * sizeof *e);
				e[i].y = ' ';
				e[i].path = q;
			}
			e[i].x = *p;
			e[i].orig = o;
		}
		qsort(e, *n, sizeof *e, cmp);
	}
	return e;
}

char *diff(Entry *e, int cached)
{
	char *s, *u = !cached && e->y == '?' ? "--no-index" : 0, c[16];
	snprintf(c, sizeof c, "-U%d", context);
	git(&s, 0, (char *[]){"git", "diff", "--no-color", "--no-ext-diff", c,
		cached ? "--cached" : u ? u : "-p", cached && base ? base : "-p", "--", u ? "/dev/null" : e->path, e->path, 0});
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

int apply(char **out, const char *d, int from, int to, int mode)
{
	char *p = patch(d, from, to, mode > 0);
	int r = p ? git(out, p, (char *[]){"git", "apply", mode > 1 ? "-R" : "--cached", mode ? "-R" : "--cached", "-", 0}) : (*out = 0, 0);
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
	char **a = malloc((2 * n + 6) * sizeof *a), **p = a + 4;
	int r;
	op += op == 2 && e[0]->y == '?';
	memcpy(a, cmd[op], sizeof *cmd);
	if (op == 1 && base) a[3] = base, *p++ = "--";
	for (; n--; e++) {
		*p++ = (*e)->path;
		if (op == 1 && (*e)->orig) *p++ = (*e)->orig;
	}
	*p = 0;
	r = git(out, 0, a);
	free(a);
	return r;
}

char *patterns(Entry **e, int n)
{
	char *s, *p;
	size_t len;
	FILE *f = open_memstream(&s, &len);
	for (; n--; e++) {
		fputc('/', f);
		for (p = (*e)->path; *p; fputc(*p++, f)) if (strchr("\\*?[", *p) || (*p == ' ' && !p[1])) fputc('\\', f);
		if (n) fputc('\n', f);
	}
	fclose(f);
	return s;
}

static int split(char ***l, int n, char *p)
{
	while (*p) {
		*l = realloc(*l, (n + 1) * sizeof **l);
		(*l)[n++] = p;
		p += strcspn(p, "\n");
		if (*p) *p++ = 0;
	}
	return n;
}

static int by(const void *a, const void *b)
{
	return strcmp(*(char **)a, *(char **)b);
}

int ignore(char **out, char *add, int sort)
{
	int fd = open(".gitignore", O_RDONLY), i, j, k, m, n;
	char *s = fd < 0 ? calloc(1, 1) : slurp(fd), **l = 0;
	FILE *f;
	if (fd >= 0) close(fd);
	for (k = n = split(&l, 0, s); k && !*l[k - 1]; k--);
	m = split(&l, n, add) - n;
	memmove(l + k, l + n, m * sizeof *l);
	for (i = k + m, n += m; i < n; i++) l[i] = "";
	for (i = 0; sort && i < n; i = j + 1) {
		for (j = i; j < n && *l[j]; j++);
		qsort(l + i, j - i, sizeof *l, by);
	}
	*out = 0;
	if ((f = fopen(".gitignore", "w"))) {
		for (i = 0; i < n; i++) fprintf(f, "%s\n", l[i]);
		fclose(f);
	} else *out = strdup("cannot write .gitignore");
	free(l);
	free(s);
	return !f;
}

int commit(char **out, const char *msg, int amend)
{
	return git(out, msg, (char *[]){"git", "commit", "-q", "-F", "-", amend ? "--amend" : 0, 0});
}

Row *history(int *n)
{
	static char *s;
	static Row *r;
	char *p, *e, **f;
	free(s);
	if (git(&s, 0, (char *[]){"git", "log", "--graph", "--exclude=refs/notes/*", "--all", "--decorate=full",
		"--format=%x1f%h%x1f%D%x1f%aN%x1f%ai%x1f%s", 0})) *s = 0;
	for (*n = 0, p = s; *p; p = e, (*n)++) {
		e = p + strcspn(p, "\n");
		if (*e) *e++ = 0;
		r = realloc(r, (*n + 1) * sizeof *r);
		r[*n] = (Row){p};
		for (f = &r[*n].graph; f < &r[*n].subject && (p = strchr(p, 037)); *++f = ++p) *p = 0;
	}
	return r;
}

int joins(const char *g, int k, int down)
{
	int n = g ? strlen(g) : 0;
	return (k < n && (g[k] == '|' || g[k] == '*')) || (k > 0 && k <= n && g[k - 1] == "\\/"[down]) || (k + 1 < n && g[k + 1] == "/\\"[down]);
}

char *repo(const char *path)
{
	char *top;
	if ((path && chdir(path)) || git(&top, 0, (char *[]){"git", "rev-parse", "--show-toplevel", 0})) return 0;
	top[strcspn(top, "\n")] = 0;
	return chdir(top) ? 0 : top;
}

