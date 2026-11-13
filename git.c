
#define _GNU_SOURCE
#include <fcntl.h>
#include <search.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
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
		setenv("GIT_CONFIG_COUNT", "1", 1);
		setenv("GIT_CONFIG_KEY_0", "core.quotePath", 1);
		setenv("GIT_CONFIG_VALUE_0", "false", 1);
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
		rewind(e);
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

static int by(const void *a, const void *b)
{
	return strcmp(*(char **)a, *(char **)b);
}

Entry *ent;
int nent;

void status(void)
{
	static char *s, *d;
	Entry *e = ent;
	char *p, *q, *o;
	int i, *n = &nent;
	free(s);
	free(d);
	d = 0;
	if (git(&s, 0, (char *[]){"git", "status", "--porcelain=v1", "-z", "-uall", 0})) *s = 0;
	for (*n = 0, p = s; *p; (*n)++) {
		e = realloc(e, (*n + 1) * sizeof *e);
		e[*n] = (Entry){p + 3, 0, p[0], p[1]};
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
		qsort(e, *n, sizeof *e, by);
	}
	ent = e;
}

char *diff(Entry *e, int cached)
{
	char *s, c[16];
	if (e && !cached && e->y == '?') return diffs(2, e, 1);
	snprintf(c, sizeof c, "-U%d", context);
	git(&s, 0, (char *[]){"git", "diff", "--no-color", "--no-ext-diff", c,
		cached ? "--cached" : "-p", cached && base ? base : "-p", "--", e ? e->path : 0, 0});
	return s;
}

/* Untracked files go into a temporary index as intent-to-add, so one git diff shows them all as new files. */
char *diffs(int sec, Entry *e, int n)
{
	char t[] = "/tmp/wgit-XXXXXX", x[24], *s, *p;
	size_t len;
	FILE *f;
	if (sec < 2) return diff(0, !sec);
	mkdtemp(t);
	snprintf(x, sizeof x, "%s/i", t);
	f = open_memstream(&s, &len);
	for (; n--; e++) if (e->y == '?') {
		fputc('"', f);
		for (p = e->path; *p; p++) {
			if (strchr("\"\\\n", *p)) fputc('\\', f);
			fputc(*p == '\n' ? 'n' : *p, f);
		}
		fputs("\"\n", f);
	}
	fclose(f);
	setenv("GIT_INDEX_FILE", x, 1);
	git(0, s, (char *[]){"git", "add", "-N", "--pathspec-from-file=-", 0});
	free(s);
	s = diff(0, 0);
	unsetenv("GIT_INDEX_FILE");
	remove(x);
	remove(t);
	return s;
}

char *patch(const char *d, int from, int to, int rev)
{
	char *r, *b, *q = 0;
	size_t n, m;
	int l = 0, h = -1, a = 0, c = 0, ob = 0, nb = 0, any = 0, chg = 0, drop = 0, top, ch;
	const char *p = d, *e;
	FILE *f = open_memstream(&r, &n), *g = 0;
	for (;; p = e, l++) {
		e = p + strcspn(p, "\n");
		e += !!*e;
		top = !strncmp(p, "diff --git ", 11);
		if (top && l > from) e = p = "";
		if (g && (top || !*p || !strncmp(p, "@@", 2))) {
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
		if (top) rewind(f), h = -1;
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

int ignore(char **out, char *add, int sort)
{
	int fd = open(".gitignore", O_RDONLY), i, j, k, m, n;
	char *s = fd < 0 ? strdup("") : slurp(fd), **l = 0;
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

Row *history(int *n, int max)
{
	static char *s;
	static Row *r;
	char *p, **f, *t, **l = 0, lim[16];
	int i, m;
	free(s);
	snprintf(lim, sizeof lim, "-n%d", max);
	if (git(&s, 0, (char *[]){"git", "log", "--graph", "--exclude=refs/notes/*", "--all", "--decorate=full", lim,
		"--format=%x1f%h%x1f%H%x1f%D%x1f%aN%x1f%ai%x1f%s", 0})) *s = 0;
	*n = split(&l, 0, s);
	r = realloc(r, (*n + 1) * sizeof *r);
	for (i = 0; i < *n; i++) {
		r[i] = (Row){p = l[i]};
		for (f = &r[i].graph; f < &r[i].subject && (p = strchr(p, 037)); *++f = ++p) *p = 0;
	}
	m = git(&t, 0, (char *[]){"git", "notes", "list", 0}) ? 0 : split(&l, 0, t);
	for (i = 0; i < m; i++) l[i] += strcspn(l[i], " ") + 1;
	qsort(l, m, sizeof *l, by);
	for (i = 0; m && i < *n; i++) r[i].note = r[i].full && bsearch(&r[i].full, l, m, sizeof *l, by);
	free(l);
	free(t);
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

typedef struct {
	char *key, last[11];
	long n, add, del, days;
	int at;
} Stat;

typedef struct {
	void *root;
	Stat **v;
	int n;
} Tab;

static Stat *get(Tab *t, char *key)
{
	void **f = tsearch(&key, &t->root, by);
	Stat *s = *f;
	if (s == (void *)&key) {
		size_t n = strlen(key) + 1;
		*f = s = malloc(sizeof *s + n);
		*s = (Stat){memcpy(s + 1, key, n)};
		t->v = realloc(t->v, ++t->n * sizeof *t->v);
		t->v[t->n - 1] = s;
	}
	return s;
}

static void bump(Tab *t, char *key, int at, long add, long del)
{
	Stat *s = get(t, key);
	if (s->at != at) s->at = at, s->n++;
	s->add += add;
	s->del += del;
}

static int rank(const void *a, const void *b)
{
	Stat *s = *(Stat **)a, *t = *(Stat **)b;
	return s->n < t->n ? 1 : s->n > t->n ? -1 : strcmp(s->key, t->key);
}

static void spark(FILE *f, long *v, int n, long max)
{
	for (; n--; v++) *v ? fprintf(f, "\xe2\x96%c", (int)(0x81 + *v * 7 / max)) : fputc(' ', f);
	fputc('\n', f);
}

char *stats(int days, long now)
{
	static const char title[][12] = {"Authors", "Files", "Directories", "File types"};
	Tab t[6] = {{0}}, *u;
	Stat *a = 0, *r;
	char *s, *o, *p, *q, *e, *d, *c, since[32];
	time_t n = now;
	long *day = 0, act[60] = {0}, grid[7][24] = {{0}}, add = 0, del = 0, x, y, max = 1, top = 1;
	long off = localtime(&n)->tm_gmtoff, today = (now + off) / 86400, old = today;
	int i, k, w, nc = 0, span, step, nb;
	size_t len;
	FILE *f = open_memstream(&o, &len);
	snprintf(since, sizeof since, "--since=%ld", (today - days + 1) * 86400 - off);
	if (git(&s, 0, (char *[]){"git", "log", "--numstat", "--no-renames", "--format=%x01%at %ad%x1f%aN",
		"--date=format:%z %u %H %Y-%m-%d", days ? since : 0, 0})) *s = 0;
	for (p = s; *p; p = e) {
		e = p + strcspn(p, "\n");
		if (*e) *e++ = 0;
		if (*p == 1) {
			day = realloc(day, (nc + 1) * sizeof *day);
			x = strtol(p + 1, &q, 10);
			y = strtol(q, &q, 10);
			if ((day[nc++] = (x + (y / 100 * 60 + y % 100) * 60) / 86400) < old) old = day[nc - 1];
			w = strtol(q, &q, 10) - 1;
			if (++grid[w][strtol(q, &q, 10)] > top) top++;
			d = q + 1;
			a = get(t, d + 11);
			if (!a->n++) memcpy(a->last, d, 10);
			if (!get(t + 4, d)->n++) a->days++;
			d[10] = 0;
			get(t + 5, d);
		} else if (a && (q = strchr(p, '\t')) && (d = strchr(q + 1, '\t'))) {
			add += x = strtol(p, 0, 10);
			del += y = strtol(q + 1, 0, 10);
			a->add += x;
			a->del += y;
			bump(t + 1, ++d, nc, x, y);
			for (q = 0, c = p = d; *p; p++) if (*p == '/') q = p, c = p + 1; else if (*p == '.') c = p + 1;
			bump(t + 3, c, nc, x, y);
			if (q) {
				*q = 0;
				bump(t + 2, d, nc, x, y);
			}
		}
	}
	fprintf(f, "Commits %d, authors %d, active days %d, lines +%ld -%ld\n", nc, t->n, t[5].n, add, del);
	span = days ? days : today - old + 1;
	step = (span + 59) / 60;
	nb = (span + step - 1) / step;
	for (i = 0; i < nc; i++) {
		k = (today - day[i]) / step;
		if (++act[nb - 1 - (k < 0 ? 0 : k < nb ? k : nb - 1)] > max) max++;
	}
	if (nc) {
		fprintf(f, "\nActivity, %d day%s per bar\n", step, &"s"[step < 2]);
		spark(f, act, nb, max);
		fputs("\n     00    06    12    18\n", f);
		for (w = 0; w < 7; w++) {
			fprintf(f, "%.3s  ", &"MonTueWedThuFriSatSun"[3 * w]);
			spark(f, grid[w], 24, top);
		}
	}
	for (u = t; u < t + 6; u++) {
		qsort(u->v, u->n, sizeof *u->v, rank);
		w = u < t + 4 && u->n;
		if (w) fprintf(f, "\ncommits      %%                  +       -  %s%s (%d)\n", u == t ? "days  last        " : "", title[u - t], u->n);
		for (i = 0; w && i < u->n && i < 20; i++) {
			r = u->v[i];
			fprintf(f, "%7ld %5.1f%% ", r->n, 100. * r->n / nc);
			for (k = 0, x = r->n * 80 / u->v[0]->n; k < 10; k++, x -= 8) x > 0 ? fprintf(f, "\xe2\x96%c", (int)(x < 8 ? 0x90 - x : 0x88)) : fputc(' ', f);
			fprintf(f, " %7ld %7ld  ", r->add, r->del);
			if (u == t) fprintf(f, "%4ld  %s  ", r->days, r->last);
			fprintf(f, "%s\n", r->key);
		}
		tdestroy(u->root, free);
		free(u->v);
	}
	free(day);
	free(s);
	fclose(f);
	return o;
}

