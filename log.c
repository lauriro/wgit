
#include <gtk/gtk.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

enum { W = 7, PAD = 4, GAP = 3, PAGE = 10000 };

static GtkWidget *list, *dv;
static GtkTreeViewColumn *graph;
static GtkListStore *store;
static GtkTextBuffer *info;
static Row *row;
static char *query, *seen;
static GtkTreeModel *filter;
static int nrow, cells, ncom, limit, pending, tagged, wip, again, busy, cancelled;
static const char title[] = "Uncommitted changes";
static double x0;
static const char sigs[] = "GUXYRBEN", *said[][2] = {
	{"good", "✓ Good signature"},
	{"warn", "✓ Good signature, unknown validity"},
	{"warn", "✓ Good signature, expired"},
	{"warn", "✓ Good signature, expired key"},
	{"warn", "✓ Good signature, revoked key"},
	{"bad", "✗ Bad signature"},
	{"dim", "Cannot check the signature"},
	{"dim", "No signature"}
}, *ink[][2] = {{"dim", "#888"}, {"hash", "#c55"}, {"good", "#4a4"}, {"warn", "#c93"}, {"bad", "#e33"}};
static const char *kind[][2] = {
	{"HEAD -> refs/heads/", "#3d6b3d\" weight=\"bold"},
	{"refs/heads/", "#3d6b3d"},
	{"refs/remotes/", "#666"},
	{"tag: refs/tags/", "#8a7420"},
	{"HEAD", "#2f6fbf"}
};

static char *line(Row *r)
{
	FILE *s;
	char *p, *e, *t, *m;
	size_t n;
	int k, l;
	s = open_memstream(&m, &n);
	fprintf(s, "<span foreground=\"#c55\" font_family=\"monospace\">%s</span>&#x2009; <span foreground=\"#888\">%.16s</span> &#x2009;", r->hash, r->date);
	for (p = r->refs; *p; p = e + 2 * !!*e) {
		e = p + strcspn(p, ",");
		for (k = 0; k < 5 && strncmp(p, kind[k][0], l = strlen(kind[k][0])); k++);
		if (k == 5 || (k == 2 && !strncmp(e - 5, "/HEAD", 5))) continue;
		if (k == 4) l = 0;
		t = g_markup_escape_text(p + l, e - p - l);
		fprintf(s, "<span background=\"%s\" foreground=\"#fff\"> %s </span> ", kind[k][1], t);
		g_free(t);
	}
	k = *r->subject == '!';
	t = g_markup_printf_escaped("%s  <span foreground=\"#4a4\">%s</span>", r->subject + k, r->author);
	fprintf(s, "%s%s", k ? "&#x200a;<span foreground=\"#e33\" weight=\"heavy\" size=\"125%\" line_height=\"0.6\" rise=\"-0.75pt\">!</span>" : "", t);
	g_free(t);
	fclose(s);
	return m;
}
static const char shape[] = "|/\\_-";
static const double geom[][4] = {{0, 0, 0, 1}, {1, 0, -1, 1}, {-1, 0, 1, 1}, {-1, 1, 1, 1}, {-1, .5, 1, .5}};
static const double pal[][3] = {{.29, .53, .91}, {.25, .68, .25}, {.88, .32, .32}, {.79, .64, .15}, {.75, .32, .75}, {.16, .65, .72}};

static void colour(cairo_t *cr, int k)
{
	const double *c = pal[k / 2 % 6];
	cairo_set_source_rgba(cr, c[0], c[1], c[2], 1);
}

static void stroke(cairo_t *cr, int k, double x1, double y1, double x2, double y2)
{
	colour(cr, k);
	cairo_move_to(cr, x1, y1);
	cairo_line_to(cr, x2, y2);
	cairo_stroke(cr);
}

static void fill(int from)
{
	char *m;
	int i, k;
	for (i = from; i < nrow; i++) {
		k = strlen(row[i].graph);
		if (k > cells) cells = k;
		if (!row[i].hash) continue;
		m = line(row + i);
		gtk_list_store_insert_with_values(store, 0, -1, 0, m, 1, i, -1);
		free(m);
		ncom++;
	}
	g_object_set(graph, "fixed-width", 2 * PAD + cells * W, NULL);
}

static int stale(void)
{
	char *t;
	int r;
	git(&t, 0, (char *[]){"git", "for-each-ref", "--include-root-refs", "--format=%(HEAD)%(objectname)%(refname)", 0});
	r = !seen || strcmp(seen, t);
	free(seen);
	seen = t;
	return r;
}

/* The changes of the tracked files are the first row, with the index -1, while there are any.
 * Nothing of them is kept: a selection of the row or a click on it reads the diff, one git run each.
 * A switch to the log view runs only this git status, so the diff of a selected row waits for a click. */
static void dirty(void)
{
	GtkTreePath *p = gtk_tree_path_new_from_indices(0, -1);
	GtkTreeIter it;
	char *o;
	int d = !git(&o, 0, (char *[]){"git", "status", "--porcelain", "-uno", 0}) && *o;
	free(o);
	if (d > wip) gtk_list_store_insert_with_values(store, 0, 0, 0, title, 1, -1, -1);
	if (d < wip && gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, p)) gtk_list_store_remove(store, &it);
	wip = d;
	if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), 0, 0)) gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), p, 0, FALSE);
	gtk_tree_path_free(p);
}

/* A reload keeps the commit chosen, by its hash, as a deepen or a cancelled fetch reads the log again: the first row
 * was chosen anew */
static void load(void)
{
	GtkTreeSelection *s = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	GtkTreeModel *m;
	GtkTreeIter it;
	char *keep = 0;
	int i;
	if (gtk_tree_selection_get_selected(s, &m, &it) && (gtk_tree_model_get(m, &it, 1, &i, -1), i >= 0)) keep = g_strdup(row[i].full);
	gtk_tree_view_set_model(GTK_TREE_VIEW(list), 0);
	gtk_list_store_clear(store);
	ncom = wip = 0;
	cells = 4;
	row = history(&nrow, limit = PAGE);
	fill(0);
	gtk_tree_view_set_model(GTK_TREE_VIEW(list), filter);
	if (keep && gtk_tree_model_get_iter_first(filter, &it))
		do {
			gtk_tree_model_get(filter, &it, 1, &i, -1);
			if (i >= 0 && !strcmp(row[i].full, keep)) {
				GtkTreePath *p = gtk_tree_model_get_path(filter, &it);
				gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), p, 0, FALSE), gtk_tree_path_free(p);
				break;
			}
		} while (gtk_tree_model_iter_next(filter, &it));
	g_free(keep);
	dirty();
}

static gboolean more(gpointer all)
{
	int from = nrow;
	pending = 0;
	if (stale()) load();
	else {
		row = history(&nrow, limit = all ? 1 << 30 : limit + PAGE);
		fill(from);
	}
	return FALSE;
}

static double col(int k)
{
	return x0 + PAD + (cells - 1 - k) * W + W / 2;
}

static void span(cairo_t *cr, int a, int b, double ya, double h)
{
	const char *p, *s;
	const double *g;
	double t0, t1, c, d, sc = h / (b - a), y0 = ya - (a + .5) * sc;
	int j, k, m;
	for (j = a; j <= b; j++) for (p = row[j].graph, t0 = j == a ? .5 : 0, t1 = j == b ? .5 : 1, k = 0; p[k]; k++) {
		c = col(k);
		if (p[k] == '*') {
			if (j == a ? joins(row[a + 1].graph, k, 1) : joins(row[b - 1].graph, k, 0)) stroke(cr, k, c, y0 + (j + t0) * sc, c, y0 + (j + t1) * sc);
			if (j > a && (m = rider(row, j, k)) >= 0) stroke(cr, m, c - GAP, y0 + j * sc, c, y0 + (j + .5) * sc);
		} else if ((s = strchr(shape, p[k]))) {
			g = geom[s - shape];
			d = j < b && k && p[k] == '/' && joined(row, j, k - 1) ? GAP : 0;
			if (g[1] != g[3]) stroke(cr, lane(row, j, k), c - (g[0] + (g[2] - g[0]) * t0) * W - d * t0, y0 + (j + t0) * sc,
				c - (g[0] + (g[2] - g[0]) * t1) * W - d * t1, y0 + (j + t1) * sc);
			else if (j < b) stroke(cr, k + 1, c - g[0] * W, y0 + (j + g[1]) * sc, c - g[2] * W, y0 + (j + g[3]) * sc);
			if (p[k] == '|' && (m = rider(row, j, k)) >= 0) stroke(cr, m, c - GAP, y0 + (j + t0) * sc, c - GAP, y0 + (j + t1) * sc);
		}
	}
}

static gboolean paint(GtkTreeView *v, cairo_t *cr)
{
	GtkTreePath *a, *b;
	GtkTreeIter it;
	GdkRectangle r;
	int i, j, k, x, y, top = 1;
	double cy;
	if ((query && *query) || !gtk_tree_view_get_visible_range(v, &a, &b)) return FALSE;
	cairo_set_line_width(cr, 2);
	for (; gtk_tree_path_compare(a, b) <= 0; gtk_tree_path_next(a), top = 0) {
		gtk_tree_view_get_background_area(v, a, graph, &r);
		gtk_tree_view_convert_bin_window_to_widget_coords(v, r.x, r.y, &x, &y);
		gtk_tree_model_get_iter(filter, &it, a);
		gtk_tree_model_get(filter, &it, 1, &i, -1);
		x0 = x;
		cy = y + r.height / 2.0;
		/* The changes stand on HEAD: the line goes to it only when HEAD is the row below */
		if (i < 0) {
			k = nrow && !strncmp(row->refs, "HEAD", 4) ? strchr(row->graph, '*') - row->graph : -1;
			if (k >= 0) stroke(cr, k, col(k), cy + 4, col(k), cy + r.height);
			cairo_set_source_rgba(cr, .88, .75, .25, 1);
			cairo_arc(cr, col(MAX(k, 0)), cy, 3.5, 0, 2 * G_PI);
			cairo_stroke(cr);
			continue;
		}
		for (j = i - 1; top && j >= 0 && !row[j].hash; j--);
		if (top && j >= 0) span(cr, j, i, cy - r.height, r.height);
		for (j = i + 1; j < nrow && !row[j].hash; j++);
		if (j < nrow) span(cr, i, j, cy, r.height);
		k = strchr(row[i].graph, '*') - row[i].graph;
		if (row[i].note) {
			cairo_set_source_rgba(cr, .88, .75, .25, 1);
			cairo_rectangle(cr, col(k) - 4, cy - 4, 8, 8);
		} else {
			colour(cr, k);
			cairo_arc(cr, col(k), cy, 3.5, 0, 2 * G_PI);
		}
		/* History goes on below the edge of a shallow clone, not fetched: three dots under it */
		if (row[i].edge) for (j = 1; j <= 3; j++) cairo_new_sub_path(cr), cairo_arc(cr, col(k), cy + 3.5 + 4 * j, 1.2, 0, 2 * G_PI);
		cairo_fill(cr);
	}
	if (!pending && ncom >= limit && *gtk_tree_path_get_indices(b) + 1 >= ncom)
		pending = g_idle_add(more, 0);
	gtk_tree_path_free(a);
	gtk_tree_path_free(b);
	return FALSE;
}

/* The graph is drawn over the rows, as the GTK 3 draw signal did after the view */
typedef GtkTreeView Rows;
typedef GtkTreeViewClass RowsClass;
G_DEFINE_TYPE(Rows, rows, GTK_TYPE_TREE_VIEW)

static void rows_snapshot(GtkWidget *w, GtkSnapshot *s)
{
	cairo_t *cr;
	GTK_WIDGET_CLASS(rows_parent_class)->snapshot(w, s);
	paint(GTK_TREE_VIEW(w), cr = gtk_snapshot_append_cairo(s, &GRAPHENE_RECT_INIT(0, 0, gtk_widget_get_width(w), gtk_widget_get_height(w))));
	cairo_destroy(cr);
}

static void rows_class_init(RowsClass *c)
{
	GTK_WIDGET_CLASS(c)->snapshot = rows_snapshot;
}

static void rows_init(Rows *r)
{
}

static void put(const char *s, int len, const char *tag)
{
	GtkTextIter it;
	char *t = g_utf8_make_valid(s, len);
	gtk_text_buffer_get_end_iter(info, &it);
	gtk_text_buffer_insert_with_tags_by_name(info, &it, t, -1, tag, NULL);
	g_free(t);
}

static void cut(char *p, char **f, int n)
{
	for (; n--; p += strlen(p) + 1) *f++ = p;
}

static void details(int t, char **f, int sig)
{
	static const char *label[][4] = {{"Commit  ", "\nParent  ", "\nAuthor  ", "  "}, {"Tag     ", " → ", "\nTagger  ", "  "}};
	static const char *mark[] = {"hash", "hash", 0, "dim", "bold"};
	int k, l;
	tagged = t;
	gtk_text_buffer_set_text(info, "", 0);
	for (k = 0; k < 4; k++) {
		put(label[t][k], -1, "dim");
		put(f[k], -1, mark[t && !k ? 4 : k]);
	}
	put("\nSigned  ", -1, "dim");
	put(said[sig][1], -1, said[sig][0]);
	if (*f[5]) put(" from ", -1, 0), put(f[5], -1, 0);
	put("\n\n", -1, 0);
	put(f[6], l = strcspn(f[6], "\n"), "bold");
	put(f[6] + l, -1, 0);
}

static void pick(GtkTreeSelection *s);

static gboolean fetched(char *o)
{
	busy = 0;
	/* A fetch cancelled here says nothing: its message is only that it was stopped */
	if (o && !cancelled) alert(o);
	free(o), cancelled = 0;
	if (o) pick(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)));
	else stale(), load();
	return FALSE;
}

/* A fetch takes long: it runs beside the window, and the log reloads when it ends */
static gpointer fetch(gpointer n)
{
	char *o;
	if (!deepen(&o, GPOINTER_TO_INT(n))) free(o), o = 0;
	g_idle_add((GSourceFunc)fetched, o);
	return 0;
}

static void go(GtkWidget *b, GtkSpinButton *n)
{
	busy = 1;
	g_object_set(b, "label", "Fetching…", "sensitive", FALSE, NULL), gtk_widget_set_visible(g_object_get_data(G_OBJECT(b), "cancel"), TRUE);
	g_thread_unref(g_thread_new(0, fetch, GINT_TO_POINTER(gtk_spin_button_get_value_as_int(n))));
}

/* A fetch's lock, which git keeps while it deepens: from this window or another, or a gk started since */
static char *lock(void)
{
	char *p;
	if (git(&p, 0, (char *[]){"git", "rev-parse", "--git-path", "shallow.lock", 0})) return free(p), (char *)0;
	return strtok(p, "\n") ? p : (free(p), (char *)0);
}

static int locked(void)
{
	char *p = lock();
	int on = p && g_file_test(p, G_FILE_TEST_EXISTS);
	free(p);
	return on;
}

/* A fetch that runs here, as its working folder is gk's, the top of the work tree, stopped: git removes its lock and
 * leaves the clone as it was */
static void cancel(void)
{
	GDir *d = g_dir_open("/proc", 0, 0);
	char *here = g_get_current_dir(), p[64], *t, *at, *name;
	const char *n;
	gsize len;
	for (; d && (n = g_dir_read_name(d));) {
		if (strspn(n, "0123456789") != strlen(n)) continue;
		snprintf(p, sizeof p, "/proc/%s/cmdline", n);
		if (!g_file_get_contents(p, &t, &len, 0)) continue;
		snprintf(p, sizeof p, "/proc/%s/cwd", n);
		name = strrchr(t, '/'), at = 0;
		if (len > 10 && !strcmp(name ? name + 1 : t, "git") && strlen(t) + 1 < len && !strcmp(t + strlen(t) + 1, "fetch")
			&& (at = g_file_read_link(p, 0)) && !strcmp(at, here))
			kill(atoi(n), SIGTERM), cancelled = busy;
		g_free(t), g_free(at);
	}
	if (d) g_dir_close(d);
	g_free(here);
}

/* While a lock is there the log looks again each second; gone, the log is read again, as after a fetch of its own */
static guint watching;

/* The number of commits to fetch as last set, while gk runs: the row is made again at each reload */
static int deeper = 5;

static void counted(GtkSpinButton *n)
{
	deeper = gtk_spin_button_get_value_as_int(n);
}

static gboolean watch(gpointer data)
{
	if (locked()) return G_SOURCE_CONTINUE;
	watching = 0;
	if (!busy) stale(), load();
	return G_SOURCE_REMOVE;
}

/* The words, the number and the button in one row, centred on one line: the words in the text stood on its baseline,
 * below the widgets beside them. A fetch going on, from here or not, shows as one with Cancel beside it */
static void deep(void)
{
	GtkTextBuffer *t = gtk_text_view_get_buffer(GTK_TEXT_VIEW(dv));
	GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *n = gtk_spin_button_new_with_range(1, 1 << 30, 1), *f, *c;
	GtkTextIter it;
	int on = busy || locked();
	g_object_set(n, "value", (double)deeper, NULL), g_signal_connect(n, "value-changed", G_CALLBACK(counted), 0);
	gtk_box_append(GTK_BOX(b), g_object_new(GTK_TYPE_LABEL, "label", "Fetch older commits:", "valign", GTK_ALIGN_CENTER, NULL));
	gtk_box_append(GTK_BOX(b), n);
	gtk_box_append(GTK_BOX(b), f = g_object_new(GTK_TYPE_BUTTON, "label", on ? "Fetching…" : "Deepen", "sensitive", !on, NULL));
	g_signal_connect(f, "clicked", G_CALLBACK(go), n);
	gtk_box_append(GTK_BOX(b), c = g_object_new(GTK_TYPE_BUTTON, "label", "Cancel", "visible", on, NULL));
	g_signal_connect_swapped(c, "clicked", G_CALLBACK(cancel), 0);
	g_object_set_data(G_OBJECT(f), "cancel", c);
	if (on && !watching) watching = g_timeout_add_seconds(1, watch, 0);
	gtk_text_buffer_get_end_iter(t, &it);
	gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(dv), b, gtk_text_buffer_create_child_anchor(t, &it));
}

static void pick(GtkTreeSelection *s)
{
	GtkTreeModel *m;
	GtkTreeIter it;
	const char *p;
	char *o, *f[9], c[16];
	int i;
	if (!gtk_tree_selection_get_selected(s, &m, &it)) return;
	snprintf(c, sizeof c, "-U%d", context);
	gtk_tree_model_get(m, &it, 1, &i, -1);
	if (i < 0) {
		tagged = 0;
		gtk_text_buffer_set_text(info, "", 0);
		put(title, -1, "bold");
		git(&o, 0, (char *[]){"git", "diff", "--no-color", "--no-ext-diff", "--default-prefix", "--submodule=short", c, "HEAD", 0});
		g_object_set(dv, "top-margin", 0, NULL), diff_files(dv, o);
	} else if (!git(&o, 0, (char *[]){"git", "show", "--format=%H%x00%P%x00%an <%ae>%x00%ai%x00%G?%x00%GS%x00%B%x00%N%x00",
		"--no-color", "--no-ext-diff", "--default-prefix", "--submodule=short", c, "--diff-merges=first-parent",
		/* The edge of a shallow clone would show its whole tree as added: in Linux that is 1.7 GB */
		row[i].edge ? "--no-patch" : "--patch", row[i].hash, 0})) {
		cut(o, f, 9);
		details(0, f, *f[4] && (p = strchr(sigs, *f[4])) ? p - sigs : 7);
		if (*g_strchomp(f[7])) put("\nNotes\n", -1, "dim"), put(f[7], -1, "note");
		while (*f[8] == '\n') f[8]++;
		/* An edge commit has no diff: the diff view says why, its text as far from the top as the details', which have no
		 * file's title bar above them */
		g_object_set(dv, "top-margin", row[i].edge ? 6 : 0, NULL);
		diff_files(dv, row[i].edge ? "The edge of a shallow clone: its parent is not here, so what it changed is not known.\n\n"
			: f[8]);
		if (row[i].edge) deep();
	}
	free(o);
}

/* More or less context around the changes, as in the commit view: one setting for both */
static void wider(gpointer more)
{
	context = more ? 2 * context : MAX(1, context / 2);
	pick(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)));
}

/* A section, so a line parts it from GTK's items */
static void menu(GMenu *m, int hunk)
{
	GMenu *s = g_menu_new();
	g_menu_append(s, "More context", "v.wider(1)");
	g_menu_append(s, "Less context", context > 1 ? "v.wider(0)" : "v.no(0)");
	g_menu_append_section(m, 0, G_MENU_MODEL(s));
	g_object_unref(s);
}

static void tag(char *name)
{
	char *o, *v, *f[7], *ref = g_strdup_printf("refs/tags/%s", name);
	int r = git(&o, 0, (char *[]){"git", "for-each-ref", "--format=%(objecttype)%00%(*objectname)%00%(taggername) %(taggeremail)%00%(taggerdate:iso)%00%00%00%(contents:subject)%0a%(contents:body)", ref, 0});
	g_free(ref);
	if (!r && !strcmp(o, "tag")) {
		cut(o, f, 7);
		f[0] = name;
		r = git(&v, 0, (char *[]){"git", "verify-tag", name, 0});
		details(1, f, !r ? 0 : strstr(v, "no signature") ? 7 : 6);
		free(v);
	}
	free(o);
}

/* A press on the selected row changes no selection, so the release reads the diff of the first row again.
 * A press that selects the row has read it already. */
static gboolean down(GtkTreeView *v)
{
	GtkTreeModel *m;
	GtkTreeIter it;
	int i = 0;
	if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(v), &m, &it)) gtk_tree_model_get(m, &it, 1, &i, -1);
	again = i < 0;
	return FALSE;
}

static gboolean chip(GtkEventController *ec, GdkEvent *ev)
{
	GtkTreeView *v = GTK_TREE_VIEW(list);
	GtkTreePath *p;
	GtkTreeViewColumn *c;
	GtkTreeIter it;
	PangoLayout *l;
	PangoAttrIterator *a;
	char *m, *t;
	double ex, ey;
	int x, y, i, j, s, e, was = tagged;
	if (gdk_event_get_event_type(ev) == GDK_BUTTON_PRESS) return down(v);
	if (gdk_event_get_event_type(ev) != GDK_BUTTON_RELEASE || gdk_button_event_get_button(ev) != 1) return FALSE;
	local(list, ev, &ex, &ey);
	gtk_tree_view_convert_widget_to_bin_window_coords(v, ex, ey, &x, &y);
	if (!gtk_tree_view_get_path_at_pos(v, x, y, &p, &c, &x, 0)) return FALSE;
	tagged = 0;
	gtk_tree_model_get_iter(filter, &it, p);
	gtk_tree_path_free(p);
	gtk_tree_model_get(filter, &it, 0, &m, 1, &j, -1);
	pango_layout_set_markup(l = gtk_widget_create_pango_layout(GTK_WIDGET(v), 0), m, -1);
	if (c != graph && pango_layout_xy_to_index(l, (x - 4) * PANGO_SCALE, PANGO_SCALE, &i, &s)) {
		a = pango_attr_list_get_iterator(pango_layout_get_attributes(l));
		do {
			pango_attr_iterator_range(a, &s, &e);
			if (s <= i && i < e && pango_attr_iterator_get(a, PANGO_ATTR_BACKGROUND)) {
				tag(t = g_strndup(pango_layout_get_text(l) + s + 1, e - s - 2));
				g_free(t);
			}
		} while (pango_attr_iterator_next(a));
		pango_attr_iterator_destroy(a);
	}
	g_object_unref(l);
	g_free(m);
	if ((was && !tagged) || (j < 0 && again)) pick(gtk_tree_view_get_selection(v));
	return FALSE;
}

static gboolean shown(GtkTreeModel *m, GtkTreeIter *it)
{
	char *s, *f;
	int i, r;
	if (!query || !*query) return TRUE;
	gtk_tree_model_get(m, it, 1, &i, -1);
	if (i < 0) return !!strstr("uncommitted changes", query);
	s = g_strdup_printf("%s %s %s %s", row[i].full, row[i].refs, row[i].subject, row[i].author);
	r = !!strstr(f = g_utf8_casefold(s, -1), query);
	g_free(s);
	g_free(f);
	return r;
}

static void find(GtkEntry *e)
{
	g_free(query);
	query = g_utf8_casefold(gtk_editable_get_text(GTK_EDITABLE(e)), -1);
	if (*query && ncom >= limit) more(GINT_TO_POINTER(1));
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter));
	g_object_set(graph, "visible", !*query, NULL);
}

GtkWidget *log_view(GtkWidget **pane)
{
	static const char *name[] = {"wider"};
	static const GCallback does[] = {G_CALLBACK(wider)};
	GtkWidget *v, *b, *c;
	GtkTreeViewColumn *col;
	GtkCellRenderer *t;
	int k;
	if (pane[0]) {
		if (stale()) load();
		else dirty();
		return pane[0];
	}
	filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT)), 0);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter), (GtkTreeModelFilterVisibleFunc)shown, 0, 0);
	list = g_object_new(rows_get_type(), "model", filter, NULL);
	graph = gtk_tree_view_column_new_with_attributes(0, gtk_cell_renderer_text_new(), NULL);
	g_object_set(graph, "sizing", GTK_TREE_VIEW_COLUMN_FIXED, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), graph);
	g_object_set(t = gtk_cell_renderer_text_new(), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	col = gtk_tree_view_column_new_with_attributes(0, t, "markup", 0, NULL);
	g_object_set(col, "sizing", GTK_TREE_VIEW_COLUMN_FIXED, "expand", TRUE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), col);
	g_object_set(list, "headers-visible", FALSE, "fixed-height-mode", TRUE, NULL);
	g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), "changed", G_CALLBACK(pick), 0);
	events(list, G_CALLBACK(chip), 0);
	v = g_object_new(GTK_TYPE_TEXT_VIEW, "name", "plain", "editable", FALSE, "monospace", TRUE, "wrap-mode", GTK_WRAP_WORD_CHAR, NULL);
	info = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	for (k = 0; k < 5; k++) gtk_text_buffer_create_tag(info, ink[k][0], "foreground", ink[k][1], NULL);
	gtk_text_buffer_create_tag(info, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
	gtk_text_buffer_create_tag(info, "note", "paragraph-background", "rgba(128,128,128,0.2)", NULL);
	pane[1] = split(GTK_ORIENTATION_VERTICAL, gtk_widget_get_parent(dv = diff_view(0, menu, 0)), v = margin(scroll(v)), 1);
	gtk_widget_set_size_request(v, -1, 150);
	b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_append(GTK_BOX(b), c = margin(g_object_new(GTK_TYPE_SEARCH_ENTRY, "placeholder-text", "Filter: hash, ref, subject, author", NULL)));
	g_signal_connect(c, "search-changed", G_CALLBACK(find), 0);
	gtk_box_append(GTK_BOX(b), scroll(list));
	pane[0] = split(GTK_ORIENTATION_HORIZONTAL, b, tabs(pane[1], diff_tools(dv)), 0);
	actions(pane[0], name, does, 1);
	stale();
	load();
	return pane[0];
}

