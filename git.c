
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

/* The pipes close on exec: a job that a hook leaves in the background must not hold the output open. */
static int run(char **out, const char *in, size_t len, char **argv, const char *index)
{
	int i[2], o[2], st;
	FILE *e = tmpfile();
	char *s, *t;
	pid_t pid;
	signal(SIGPIPE, SIG_IGN);
	if (!e || pipe2(i, O_CLOEXEC) || pipe2(o, O_CLOEXEC)) {
		if (out) *out = strdup("cannot run git");
		return -1;
	}
	if (!(pid = fork())) {
		dup2(i[0], 0);
		dup2(o[1], 1);
		dup2(fileno(e), 2);
		if (index) setenv("GIT_INDEX_FILE", index, 1);
		setenv("GIT_LITERAL_PATHSPECS", "1", 1);
		setenv("GIT_CONFIG_COUNT", "1", 1);
		setenv("GIT_CONFIG_KEY_0", "core.quotePath", 1);
		setenv("GIT_CONFIG_VALUE_0", "false", 1);
		execvp(*argv, argv);
		_exit(127);
	}
	close(i[0]);
	close(o[1]);
	if (in) write(i[1], in, len);
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

int git(char **out, const char *in, char **argv)
{
	return run(out, in, in ? strlen(in) : 0, argv, 0);
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
	int i, n;
	free(s);
	free(d);
	d = 0;
	if (git(&s, 0, (char *[]){"git", "status", "--porcelain=v1", "-z", "-uall", 0})) *s = 0;
	for (n = 0, p = s; *p; n++) {
		e = realloc(e, (n + 1) * sizeof *e);
		e[n] = (Entry){p + 3, 0, p[0], p[1]};
		p += strlen(p) + 1;
		if (*e[n].path && (strchr("RC", e[n].x) || strchr("RC", e[n].y))) {
			e[n].orig = p;
			p += strlen(p) + 1;
		}
		if (base && e[n].x != '?') {
			e[n].x = ' ';
			e[n].orig = 0;
			if (e[n].y == ' ') n--;
		}
	}
	if (base && !git(&d, 0, (char *[]){"git", "diff", "--cached", "--name-status", "-z", base, 0})) {
		for (p = d; *p; p = q + strlen(q) + 1) {
			q = p + strlen(p) + 1;
			o = strchr("RC", *p) ? q : 0;
			if (o) q += strlen(q) + 1;
			for (i = 0; i < n && strcmp(e[i].path, q); i++);
			if (i == n) {
				e = realloc(e, ++n * sizeof *e);
				e[i].y = ' ';
				e[i].path = q;
			}
			e[i].x = *p;
			e[i].orig = o;
		}
		qsort(e, n, sizeof *e, by);
	}
	ent = e;
	nent = n;
}

/* Each diff names its files as a/ and b/ and gives a submodule a plain diff, whatever the config of the user says. */
static char *read_diff(Entry *e, int cached, const char *index)
{
	char *s, c[16];
	snprintf(c, sizeof c, "-U%d", context);
	run(&s, 0, 0, (char *[]){"git", "diff", "--no-color", "--no-ext-diff", "--no-textconv", "--default-prefix", "--submodule=short", c,
		cached ? "--cached" : "-p", cached && base ? base : "-p", "--", e ? e->path : 0, 0}, index);
	return s;
}

char *diff(Entry *e, int cached)
{
	return e && !cached && e->y == '?' ? diffs(2, e, 1) : read_diff(e, cached, 0);
}

/* Untracked files go into a temporary index as intent-to-add, so one git diff shows them all as new files. */
char *diffs(int sec, Entry *e, int n)
{
	char t[] = "/tmp/wgit-XXXXXX", x[24], *s;
	size_t len;
	FILE *f;
	if (sec < 2) return diff(0, !sec);
	mkdtemp(t);
	snprintf(x, sizeof x, "%s/i", t);
	f = open_memstream(&s, &len);
	for (; n--; e++) if (e->y == '?') fwrite(e->path, 1, strlen(e->path) + 1, f);
	fclose(f);
	run(0, s, len, (char *[]){"git", "add", "-N", "--pathspec-from-file=-", "--pathspec-file-nul", 0}, x);
	free(s);
	s = read_diff(0, 0, x);
	remove(x);
	remove(t);
	return s;
}

/* A file with no chosen change leaves nothing in the patch: its header waits at hd for its first chosen hunk.
 * To unstage or revert, a rename or a copy becomes a change of the new name, so a part of its changes does not undo it.
 * git apply --recount counts the lines of a hunk, but it looks for the hunk at the start number of the side it makes.
 * u is how far the unchosen lines above in the file move that number. */
char *patch(const char *d, int from, int to, int rev)
{
	char *r, *b, *q;
	size_t n, m;
	int l = 0, h = -1, chg = 0, drop = 0, ren = 0, u = 0, top, ch, i;
	const char *p = d, *e, *s, *hd = d, *re = 0;
	FILE *f = open_memstream(&r, &n), *g = 0;
	for (;; p = e, l++) {
		e = p + strcspn(p, "\n");
		e += !!*e;
		top = !*p || !strncmp(p, "diff --", 7) || *p == '*';
		if (g && (top || !strncmp(p, "@@", 2))) {
			fclose(g);
			g = 0;
			if (chg) {
				for (; *hd != '@'; hd = s) {
					s = hd + strcspn(hd, "\n") + 1;
					if (rev && (!strncmp(hd, "rename ", 7) || !strncmp(hd, "copy ", 5))) ren = 1;
					else if (ren && !strncmp(hd, "--- ", 4)) i = s[4] == '"', fprintf(f, "--- %sa%.*s", &"\""[!i], (int)strcspn(s, "\n") - 4 - i, s + 5 + i);
					else fwrite(hd, 1, s - hd, f);
				}
				fputs(b, f);
			}
			free(b);
		}
		if (top) h = -1, ren = u = 0, hd = p;
		if (!*p) break;
		ch = *p;
		if (!strncmp(p, "@@", 2)) {
			h = l;
			chg = drop = 0;
			g = open_memstream(&b, &m);
			i = strtol(p + 4, &q, 10);
			fprintf(g, "@@ -%d +%d @@\n", i + rev * u, (int)strtol(strchr(q, '+'), 0, 10) - !rev * u);
		} else if (h < 0) continue;
		else if (ch == '\\') {
			/* The marker is true only at the end of a side. A kept - line is not the end
			 * when a + line after it stays as context, so it gets its newline back. */
			for (s = e, i = l; rev && !drop && *s == '+'; s += strcspn(s, "\n"), s += !!*s) drop = ++i > (unsigned)to;
			if (!drop) fwrite(p, 1, e - p, g);
			if (re) {
				fputc('+', g);
				fwrite(re + 1, 1, p - re - 1, g);
				re = 0;
			}
		} else if ((ch == '+' || ch == '-') && !(to < 0 ? h == from : l >= from && l <= to)) {
			u += ch == '+' ? 1 : -1;
			if (!(drop = ch == (rev ? '-' : '+'))) {
				/* An unchosen - line with no newline cannot be context above a chosen + line:
				 * it is removed and added again with a newline. */
				re = !rev && *e == '\\' && from > l ? p : 0;
				fputc(re ? '-' : ' ', g);
				fwrite(p + 1, 1, e - p - 1, g);
			}
		} else {
			fwrite(p, 1, e - p, g);
			chg |= ch == '+' || ch == '-';
			drop = 0;
		}
	}
	fclose(f);
	if (n) return r;
	free(r);
	return 0;
}

int apply(char **out, const char *d, int from, int to, int mode)
{
	char *p = patch(d, from, to, mode > 0);
	int r = p ? git(out, p, (char *[]){"git", "apply", "--recount", mode ? "-R" : "--cached", mode == 1 ? "--cached" : "--", "-", 0}) : (*out = 0, 0);
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
		if ((*e)->orig && (op == 1 || (op == 0 && (*e)->y == 'R'))) *p++ = (*e)->orig;
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
	FILE *f = fopen(".gitignore", "r");
	int i, j, k, m, n;
	char *s = f ? slurp(fileno(f)) : strdup(""), **l = 0;
	if (f) fclose(f);
	for (k = n = split(&l, 0, s); k && !*l[k - 1]; k--);
	m = split(&l, n, add) - n;
	memmove(l + k, l + n, m * sizeof *l);
	for (i = k + m, n += m; i < n; i++) l[i] = "";
	/* A block with a ! pattern keeps its order: there the order has a meaning */
	for (i = 0; sort && i < n; i = j + 1) {
		for (j = i, k = 0; j < n && *l[j]; j++) k |= *l[j] == '!';
		if (!k) qsort(l + i, j - i, sizeof *l, by);
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
	char *key;
	long n, add, del;
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

static int rank(const void *a, const void *b)
{
	Stat *s = *(Stat **)a, *t = *(Stat **)b;
	return s->n < t->n ? 1 : s->n > t->n ? -1 : strcmp(s->key, t->key);
}

/* A count field after a tab: names escape their tabs, so the view finds it as it is */
static void counts(FILE *f, long add, long del)
{
	if (add || del) fputc('\t', f);
	if (add) fprintf(f, "+%ld", add);
	if (del) fprintf(f, "-%ld", del);
}

/* A name with its control characters escaped, so that it cannot split its row */
static void name(FILE *f, const char *p)
{
	static const char esc[] = "\n\r\t\\";
	for (const char *x; *p; p++) {
		if ((x = strchr(esc, *p))) fprintf(f, "\\%c", "nrt\\"[x - esc]);
		else fputc(*p, f);
	}
}

/* The extension of a file's name, not of its folder's; a name with none, as Makefile or .gitignore, is one group */
static char *extension(char *path)
{
	char *p = strrchr(path, '/'), *q;
	p = p ? p + 1 : path;
	q = strrchr(p, '.');
	return q && q != p && q[1] ? q + 1 : "(no extension)";
}

/* Sections from one log: a heading with its total and lines, then its rows ranked by count, each with its share */
char *stats(int days, long now, const char *path)
{
	static const char *const head[] = { "Commits: %ld", "Lines changed by file type: %ld", "Most frequently changed files (top 20)" };
	/* How many rows a section shows, 0 for all */
	static const int most[] = { 0, 0, 20 };
	enum { N = sizeof head / sizeof *head };
	Tab t[N] = {{0}};
	Stat *author = 0, *v;
	char *s, *o, *p, *q, *e, since[32];
	time_t n = now;
	long add = 0, del = 0, a, d, nc = 0, off = localtime(&n)->tm_gmtoff;
	int i, k;
	size_t len;
	FILE *f = open_memstream(&o, &len);
	snprintf(since, sizeof since, "--since=%ld", ((now + off) / 86400 - days + 1) * 86400 - off);
	if (git(&s, 0, (char *[]){"git", "log", "--numstat", "-z", "--no-renames", "--no-ext-diff", "--no-textconv",
		"--format=%x01%aN <%aE>", days ? since : "--no-renames", "--", (char *)path, 0})) *s = 0;
	for (p = s; *p; p = e) {
		e = p + strlen(p) + 1;
		while (*p == '\n') p++;
		if (*p == 1) nc++, (author = get(t, p + 1))->n++;
		else if (author && (q = strchr(p, '\t'))) {
			/* added, removed and the path; a binary file's counts are - */
			a = strtol(p, 0, 10), d = strtol(q + 1, 0, 10), q = strchr(q + 1, '\t') + 1;
			add += a, del += d, author->add += a, author->del += d;
			v = get(t + 2, q), v->n++, v->add += a, v->del += d;
			if (a || d) v = get(t + 1, extension(q)), v->n += a + d, v->add += a, v->del += d;
		}
	}
	long total[N] = { nc, add + del, nc }, plus[N] = { add, add }, minus[N] = { del, del };
	for (k = 0; k < N; k++) {
		if (k) fputc('\n', f);
		fprintf(f, head[k], total[k]);
		counts(f, plus[k], minus[k]);
		fputs(t[k].n ? "\n\n" : "\n", f);
		qsort(t[k].v, t[k].n, sizeof *t[k].v, rank);
		for (i = 0; i < t[k].n && (!most[k] || i < most[k]); i++) {
			v = t[k].v[i];
			fprintf(f, "%6ld %5.1f%%  ", v->n, 100.0 * v->n / total[k]);
			name(f, v->key);
			counts(f, v->add, v->del);
			fputc('\n', f);
		}
		tdestroy(t[k].root, free);
		free(t[k].v);
	}
	free(s);
	fclose(f);
	return o;
}

