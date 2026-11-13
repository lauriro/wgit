
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

enum { W = 7, PAD = 4 };

static GtkWidget *list, *dv, *sum;
static GtkTreeViewColumn *graph;
static GtkListStore *store;
static GtkTextBuffer *info;
static Row *row;
static char *text, *query;
static GtkTreeModel *filter;
static int nrow, cells;
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
	GString *s = g_string_new(0);
	char *p, *e, *t;
	int k, l;
	g_string_append_printf(s, "<span foreground=\"#c55\" font_family=\"monospace\">%s</span> ", r->hash);
	for (p = r->refs; *p; p = e + 2 * !!*e) {
		e = p + strcspn(p, ",");
		for (k = 0; k < 5 && strncmp(p, kind[k][0], l = strlen(kind[k][0])); k++);
		if (k == 5 || (k == 2 && !strncmp(e - 5, "/HEAD", 5))) continue;
		if (k == 4) l = 0;
		t = g_markup_escape_text(p + l, e - p - l);
		g_string_append_printf(s, "<span background=\"%s\" foreground=\"#fff\"> %s </span> ", kind[k][1], t);
		g_free(t);
	}
	k = *r->subject == '!';
	t = g_markup_printf_escaped("%s  <span foreground=\"#4a4\">%s</span>  <span foreground=\"#39a\">%.16s</span>", r->subject + k, r->author, r->date);
	g_string_append_printf(s, "%s%s", k ? "<span foreground=\"#e33\" weight=\"bold\">!</span>" : "", t);
	g_free(t);
	return g_string_free(s, FALSE);
}
static const char shape[] = "|/\\_-";
static const double geom[][4] = {{0, 0, 0, 1}, {1, 0, -1, 1}, {-1, 0, 1, 1}, {-1, 1, 1, 1}, {-1, .5, 1, .5}};
static const double pal[][3] = {{.29, .53, .91}, {.25, .68, .25}, {.88, .32, .32}, {.79, .64, .15}, {.75, .32, .75}, {.16, .65, .72}};

static void colour(cairo_t *cr, int k)
{
	const double *c = pal[k / 2 % 6];
	cairo_set_source_rgb(cr, c[0], c[1], c[2]);
}

static void stroke(cairo_t *cr, int k, double x1, double y1, double x2, double y2)
{
	colour(cr, k);
	cairo_move_to(cr, x1, y1);
	cairo_line_to(cr, x2, y2);
	cairo_stroke(cr);
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
			if (g[1] != g[3]) stroke(cr, k + (p[k] != '|'), c - (g[0] + (g[2] - g[0]) * t0) * W, y0 + (j + t0) * sc,
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
		for (j = i - 1; top && j >= 0 && !row[j].hash; j--);
		if (top && j >= 0) span(cr, j, i, cy - r.height, r.height);
		for (j = i + 1; j < nrow && !row[j].hash; j++);
		if (j < nrow) span(cr, i, j, cy, r.height);
		k = strchr(row[i].graph, '*') - row[i].graph;
		if (row[i].note) {
			cairo_set_source_rgb(cr, .88, .75, .25);
			cairo_rectangle(cr, col(k) - 4, cy - 4, 8, 8);
		} else {
			colour(cr, k);
			cairo_arc(cr, col(k), cy, 3.5, 0, 2 * G_PI);
		}
		cairo_fill(cr);
	}
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

static void field(const char *label, const char *value, const char *tag)
{
	put(label, -1, "dim");
	put(value, -1, tag);
}

static void sign(int k, const char *who, const char *msg)
{
	int l = strcspn(msg, "\n");
	put("\nSigned  ", -1, "dim");
	put(said[k][1], -1, said[k][0]);
	if (*who) put(" from ", -1, 0), put(who, -1, 0);
	put("\n\n", -1, 0);
	put(msg, l, "bold");
	put(msg + l, -1, 0);
}

static int tag(const char *name)
{
	char *o, *v, *f[5], *p, *ref = g_strconcat("refs/tags/", name, NULL);
	int k, r = git(&o, 0, (char *[]){"git", "for-each-ref", "--format=%(objecttype)%00%(*objectname)%00%(taggername) %(taggeremail)%00%(taggerdate:iso)%00%(contents:subject)%0a%(contents:body)", ref, 0});
	g_free(ref);
	if (r || strcmp(o, "tag")) return free(o), 0;
	for (p = o, k = 0; k < 5; k++, p += strlen(p) + 1) f[k] = p;
	r = git(&v, 0, (char *[]){"git", "verify-tag", (char *)name, 0});
	gtk_text_buffer_set_text(info, "", 0);
	field("Tag     ", name, "bold");
	field(" → ", f[1], "hash");
	field("\nTagger  ", f[2], 0);
	field("  ", f[3], "dim");
	sign(!r ? 0 : strstr(v, "no signature") ? 7 : 6, "", f[4]);
	free(o);
	free(v);
	return 1;
}

static gboolean chip(GtkTreeView *v, GdkEventButton *ev)
{
	GtkTreePath *p;
	GtkTreeViewColumn *c;
	GtkTreeIter it;
	PangoLayout *l;
	PangoAttrIterator *a;
	char *m, *t;
	int x, i, s, e;
	if (ev->button != 1 || !gtk_tree_view_get_path_at_pos(v, ev->x, ev->y, &p, &c, &x, 0)) return FALSE;
	gtk_tree_model_get_iter(filter, &it, p);
	gtk_tree_path_free(p);
	gtk_tree_model_get(filter, &it, 0, &m, -1);
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
	return FALSE;
}

static void pick(GtkTreeSelection *s)
{
	GtkTreeModel *m;
	GtkTreeIter it;
	char *o, *f[8], *p;
	int i, k, n, a, d;
	if (!gtk_tree_selection_get_selected(s, &m, &it)) return;
	gtk_tree_model_get(m, &it, 1, &i, -1);
	if (!row[i].hash) return;
	if (!git(&o, 0, (char *[]){"git", "show", "-s", "--format=%H%x00%P%x00%an <%ae>%x00%ai%x00%G?%x00%GS%x00%B%x00%N", row[i].hash, 0})) {
		for (p = o, k = 0; k < 8; k++, p += strlen(p) + 1) f[k] = p;
		gtk_text_buffer_set_text(info, "", 0);
		field("Commit  ", f[0], "hash");
		field("\nParent  ", f[1], "hash");
		field("\nAuthor  ", f[2], 0);
		field("  ", f[3], "dim");
		sign(*f[4] && strchr(sigs, *f[4]) ? strchr(sigs, *f[4]) - sigs : 7, f[5], f[6]);
		if (*g_strchomp(f[7])) field("\nNotes\n", f[7], "note");
	}
	free(o);
	free(text);
	git(&text, 0, (char *[]){"git", "show", "--format=", "--no-color", "--no-ext-diff", "--diff-merges=first-parent", row[i].hash, 0});
	n = diff_files(dv, text, &a, &d);
	o = g_strdup_printf("%d file%s, +%d −%d", n, n == 1 ? "" : "s", a, d);
	gtk_label_set_text(GTK_LABEL(sum), o);
	g_free(o);
}

static void all(gpointer hide)
{
	diff_fold(dv, GPOINTER_TO_INT(hide));
}

static gboolean shown(GtkTreeModel *m, GtkTreeIter *it)
{
	char *s, *f;
	int i, r;
	if (!query || !*query) return TRUE;
	gtk_tree_model_get(m, it, 1, &i, -1);
	if (!row[i].hash) return FALSE;
	s = g_strjoin(" ", row[i].hash, row[i].refs, row[i].subject, row[i].author, NULL);
	r = !!strstr(f = g_utf8_casefold(s, -1), query);
	g_free(s);
	g_free(f);
	return r;
}

static void find(GtkEntry *e)
{
	g_free(query);
	query = g_utf8_casefold(gtk_entry_get_text(e), -1);
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter));
	gtk_tree_view_column_set_visible(graph, !*query);
}

static void load(void)
{
	GtkTreePath *p;
	char *m;
	int i, k;
	gtk_tree_view_set_model(GTK_TREE_VIEW(list), 0);
	gtk_list_store_clear(store);
	cells = 0;
	row = history(&nrow, 1 << 30);
	for (i = 0; i < nrow; i++) {
		k = strlen(row[i].graph);
		if (k > cells) cells = k;
		if (!row[i].hash) continue;
		m = line(row + i);
		gtk_list_store_insert_with_values(store, 0, -1, 0, m, 1, i, -1);
		g_free(m);
	}
	gtk_tree_view_set_model(GTK_TREE_VIEW(list), filter);
	gtk_tree_view_column_set_fixed_width(graph, 2 * PAD + cells * W);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), p = gtk_tree_path_new_first(), 0, FALSE);
	gtk_tree_path_free(p);
}

GtkWidget *log_view(GtkWidget **pane)
{
	static const char *fold[] = {"Expand all", "Collapse all"};
	GtkWidget *v, *r, *b, *c;
	GtkTreeViewColumn *col;
	GtkCellRenderer *t;
	int k;
	if (pane[0]) {
		load();
		return pane[0];
	}
	filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT)), 0);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter), (GtkTreeModelFilterVisibleFunc)shown, 0, 0);
	list = gtk_tree_view_new_with_model(filter);
	graph = gtk_tree_view_column_new_with_attributes(0, gtk_cell_renderer_text_new(), NULL);
	gtk_tree_view_column_set_sizing(graph, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), graph);
	g_signal_connect_after(list, "draw", G_CALLBACK(paint), 0);
	g_object_set(t = gtk_cell_renderer_text_new(), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	col = gtk_tree_view_column_new_with_attributes(0, t, "markup", 0, NULL);
	gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_expand(col, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), col);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);
	gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(list), TRUE);
	g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), "changed", G_CALLBACK(pick), 0);
	g_signal_connect(list, "button-release-event", G_CALLBACK(chip), 0);
	v = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(v), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(v), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v), GTK_WRAP_WORD_CHAR);
	info = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	for (k = 0; k < 5; k++) gtk_text_buffer_create_tag(info, ink[k][0], "foreground", ink[k][1], NULL);
	gtk_text_buffer_create_tag(info, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
	gtk_text_buffer_create_tag(info, "note", "paragraph-background", "rgba(128,128,128,0.2)", NULL);
	r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_pack_start(GTK_BOX(r), dv = diff_view(0, 0), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(r), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), FALSE, FALSE, 0);
	for (k = 0; k < 2; k++) {
		gtk_box_pack_start(GTK_BOX(b), c = gtk_button_new_with_label(fold[k]), FALSE, FALSE, 0);
		g_signal_connect_swapped(c, "clicked", G_CALLBACK(all), GINT_TO_POINTER(k));
	}
	gtk_box_pack_start(GTK_BOX(b), sum = gtk_label_new(0), FALSE, FALSE, 6);
	gtk_container_set_border_width(GTK_CONTAINER(r), 6);
	pane[0] = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	pane[1] = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_pack1(GTK_PANED(pane[1]), v = scroll(v), FALSE, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(v), 6);
	gtk_paned_pack2(GTK_PANED(pane[1]), r, TRUE, FALSE);
	b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(b), c = gtk_search_entry_new(), FALSE, FALSE, 0);
	gtk_entry_set_placeholder_text(GTK_ENTRY(c), "Filter: hash, ref, subject, author");
	gtk_widget_set_margin_start(c, 6);
	gtk_widget_set_margin_end(c, 6);
	gtk_widget_set_margin_top(c, 6);
	gtk_widget_set_margin_bottom(c, 6);
	g_signal_connect(c, "search-changed", G_CALLBACK(find), 0);
	gtk_box_pack_start(GTK_BOX(b), scroll(list), TRUE, TRUE, 0);
	gtk_paned_pack1(GTK_PANED(pane[0]), b, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[0]), tabs(pane[1]), TRUE, TRUE);
	load();
	return pane[0];
}

