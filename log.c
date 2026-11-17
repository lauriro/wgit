
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

enum { W = 7, PAD = 4, PAGE = 10000 };

static GtkWidget *list, *dv;
static GtkTreeViewColumn *graph;
static GtkListStore *store;
static GtkTextBuffer *info;
static Row *row;
static char *query, *seen;
static GtkTreeModel *filter;
static int nrow, cells, ncom, limit, pending, tagged, wip, again;
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

static void load(void)
{
	gtk_tree_view_set_model(GTK_TREE_VIEW(list), 0);
	gtk_list_store_clear(store);
	ncom = wip = 0;
	cells = 4;
	row = history(&nrow, limit = PAGE);
	fill(0);
	gtk_tree_view_set_model(GTK_TREE_VIEW(list), filter);
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
	double t0, t1, c, sc = h / (b - a), y0 = ya - (a + .5) * sc;
	int j, k;
	for (j = a; j <= b; j++) for (p = row[j].graph, t0 = j == a ? .5 : 0, t1 = j == b ? .5 : 1, k = 0; p[k]; k++) {
		c = col(k);
		if (p[k] == '*') {
			if (j == a ? joins(row[a + 1].graph, k, 1) : joins(row[b - 1].graph, k, 0)) stroke(cr, k, c, y0 + (j + t0) * sc, c, y0 + (j + t1) * sc);
		} else if ((s = strchr(shape, p[k]))) {
			g = geom[s - shape];
			if (g[1] != g[3]) stroke(cr, lane(row, j, k), c - (g[0] + (g[2] - g[0]) * t0) * W, y0 + (j + t0) * sc,
				c - (g[0] + (g[2] - g[0]) * t1) * W, y0 + (j + t1) * sc);
			else if (j < b) stroke(cr, k + 1, c - g[0] * W, y0 + (j + g[1]) * sc, c - g[2] * W, y0 + (j + g[3]) * sc);
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
		diff_files(dv, o);
	} else if (!git(&o, 0, (char *[]){"git", "show", "--format=%H%x00%P%x00%an <%ae>%x00%ai%x00%G?%x00%GS%x00%B%x00%N%x00",
		"--no-color", "--no-ext-diff", "--default-prefix", "--submodule=short", c, "--diff-merges=first-parent",
		/* The edge of a shallow clone would show its whole tree as added: in Linux that is 1.7 GB */
		row[i].edge ? "--no-patch" : "--patch", row[i].hash, 0})) {
		cut(o, f, 9);
		details(0, f, *f[4] && (p = strchr(sigs, *f[4])) ? p - sigs : 7);
		if (*g_strchomp(f[7])) put("\nNotes\n", -1, "dim"), put(f[7], -1, "note");
		while (*f[8] == '\n') f[8]++;
		/* An edge commit has no diff: the diff view says why */
		diff_files(dv, row[i].edge ? "The edge of a shallow clone: its parent is not here, so what it changed is not known.\n"
			"git fetch --deepen=<commits> brings older history.\n" : f[8]);
	}
	free(o);
}

/* More or less context around the changes, as in the commit view: one setting for both */
static void wider(GtkWidget *w, gpointer more)
{
	context = more ? 2 * context : MAX(1, context / 2);
	pick(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)));
}

static void menu(GtkMenu *m, int hunk)
{
	static const char *name[] = {"Less context", "More context"};
	GtkWidget *i;
	gtk_menu_shell_prepend(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
	for (int k = 0; k < 2; k++) {
		gtk_menu_shell_prepend(GTK_MENU_SHELL(m), i = gtk_menu_item_new_with_label(name[k]));
		gtk_widget_set_sensitive(i, k || context > 1);
		g_signal_connect(i, "activate", G_CALLBACK(wider), GINT_TO_POINTER(k));
	}
	gtk_widget_show_all(GTK_WIDGET(m));
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

static gboolean chip(GtkTreeView *v, GdkEventButton *ev)
{
	GtkTreePath *p;
	GtkTreeViewColumn *c;
	GtkTreeIter it;
	PangoLayout *l;
	PangoAttrIterator *a;
	char *m, *t;
	int x, i, j, s, e, was = tagged;
	if (ev->button != 1 || !gtk_tree_view_get_path_at_pos(v, ev->x, ev->y, &p, &c, &x, 0)) return FALSE;
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
	query = g_utf8_casefold(gtk_entry_get_text(e), -1);
	if (*query && ncom >= limit) more(GINT_TO_POINTER(1));
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter));
	g_object_set(graph, "visible", !*query, NULL);
}

GtkWidget *log_view(GtkWidget **pane)
{
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
	list = gtk_tree_view_new_with_model(filter);
	graph = gtk_tree_view_column_new_with_attributes(0, gtk_cell_renderer_text_new(), NULL);
	g_object_set(graph, "sizing", GTK_TREE_VIEW_COLUMN_FIXED, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), graph);
	g_signal_connect_after(list, "draw", G_CALLBACK(paint), 0);
	g_object_set(t = gtk_cell_renderer_text_new(), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	col = gtk_tree_view_column_new_with_attributes(0, t, "markup", 0, NULL);
	g_object_set(col, "sizing", GTK_TREE_VIEW_COLUMN_FIXED, "expand", TRUE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), col);
	g_object_set(list, "headers-visible", FALSE, "fixed-height-mode", TRUE, NULL);
	g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), "changed", G_CALLBACK(pick), 0);
	g_signal_connect(list, "button-press-event", G_CALLBACK(down), 0);
	g_signal_connect(list, "button-release-event", G_CALLBACK(chip), 0);
	v = g_object_new(GTK_TYPE_TEXT_VIEW, "name", "plain", "editable", FALSE, "monospace", TRUE, "wrap-mode", GTK_WRAP_WORD_CHAR, NULL);
	info = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	for (k = 0; k < 5; k++) gtk_text_buffer_create_tag(info, ink[k][0], "foreground", ink[k][1], NULL);
	gtk_text_buffer_create_tag(info, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
	gtk_text_buffer_create_tag(info, "note", "paragraph-background", "rgba(128,128,128,0.2)", NULL);
	pane[0] = split(GTK_ORIENTATION_HORIZONTAL);
	pane[1] = split(GTK_ORIENTATION_VERTICAL);
	gtk_paned_pack1(GTK_PANED(pane[1]), gtk_widget_get_parent(dv = diff_view(0, menu, 0)), TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[1]), v = scroll(v), FALSE, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(v), 6);
	gtk_widget_set_size_request(v, -1, 150);
	b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(b), c = g_object_new(GTK_TYPE_SEARCH_ENTRY, "placeholder-text", "Filter: hash, ref, subject, author", "margin", 6, NULL), FALSE, FALSE, 0);
	g_signal_connect(c, "search-changed", G_CALLBACK(find), 0);
	gtk_box_pack_start(GTK_BOX(b), scroll(list), TRUE, TRUE, 0);
	gtk_paned_pack1(GTK_PANED(pane[0]), b, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[0]), tabs(pane[1], diff_tools(dv)), TRUE, TRUE);
	stale();
	load();
	return pane[0];
}

