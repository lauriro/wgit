
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

enum { W = 7, PAD = 4 };

static GtkWidget *list, *dv;
static GtkTreeViewColumn *graph;
static GtkListStore *store;
static GtkTextBuffer *info;
static Row *row;
static char *text;
static int nrow;
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

static int at(int i, int k)
{
	return i >= 0 && i < nrow && k < (int)strlen(row[i].graph) ? row[i].graph[k] : 0;
}

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

static gboolean paint(GtkTreeView *v, cairo_t *cr)
{
	GtkTreePath *a, *b;
	GtkTreeIter it;
	GdkRectangle r;
	const char *p, *s;
	const double *g;
	int i, k, x, y, ch;
	double cx, h;
	if (!gtk_tree_view_get_visible_range(v, &a, &b)) return FALSE;
	cairo_set_line_width(cr, 2);
	for (; gtk_tree_path_compare(a, b) <= 0; gtk_tree_path_next(a)) {
		gtk_tree_view_get_background_area(v, a, graph, &r);
		gtk_tree_view_convert_bin_window_to_widget_coords(v, r.x, r.y, &x, &y);
		gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, a);
		gtk_tree_model_get(GTK_TREE_MODEL(store), &it, 1, &i, -1);
		for (p = row[i].graph, h = r.height, k = 0; p[k]; k++) {
			cx = x + PAD + k * W + W / 2;
			if (p[k] == '*') {
				if ((ch = at(i - 1, k)) == '|' || ch == '*') stroke(cr, k, cx, y, cx, y + h / 2);
				if ((ch = at(i + 1, k)) == '|' || ch == '*') stroke(cr, k, cx, y + h / 2, cx, y + h);
				colour(cr, k);
				cairo_arc(cr, cx, y + h / 2, 3.5, 0, 2 * G_PI);
				cairo_fill(cr);
			} else if ((s = strchr(shape, p[k]))) {
				g = geom[s - shape];
				stroke(cr, k + (p[k] != '|'), cx + g[0] * W, y + g[1] * h, cx + g[2] * W, y + g[3] * h);
			}
		}
	}
	gtk_tree_path_free(a);
	gtk_tree_path_free(b);
	return FALSE;
}

static void pick(GtkTreeSelection *s)
{
	GtkTreeModel *m;
	GtkTreeIter it;
	char *o;
	int i;
	if (!gtk_tree_selection_get_selected(s, &m, &it)) return;
	gtk_tree_model_get(m, &it, 1, &i, -1);
	if (!row[i].hash) return;
	git(&o, 0, (char *[]){"git", "show", "-s", "--format=Commit  %H%nParent  %p%nAuthor  %an <%ae>  %ai%nSigned  %G? %GS%n%n%B%N", row[i].hash, 0});
	gtk_text_buffer_set_text(info, o, -1);
	free(o);
	free(text);
	git(&text, 0, (char *[]){"git", "show", "--format=", "--no-color", "--no-ext-diff", "--diff-merges=first-parent", row[i].hash, 0});
	diff_show(dv, text);
}

static void load(void)
{
	GtkTreePath *p;
	char *m;
	int i, k, w = 0;
	gtk_list_store_clear(store);
	row = history(&nrow);
	for (i = 0; i < nrow; i++) {
		k = strlen(row[i].graph);
		if (k > w) w = k;
		m = row[i].hash ? line(row + i) : g_strdup("");
		gtk_list_store_insert_with_values(store, 0, -1, 0, m, 1, i, -1);
		g_free(m);
	}
	gtk_tree_view_column_set_fixed_width(graph, 2 * PAD + w * W);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), p = gtk_tree_path_new_first(), 0, FALSE);
	gtk_tree_path_free(p);
}

GtkWidget *log_view(GtkWidget **pane)
{
	GtkWidget *v, *r, *b, *c;
	GtkTreeViewColumn *col;
	GtkCellRenderer *t;
	if (pane[0]) {
		load();
		return pane[0];
	}
	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT)));
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
	v = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(v), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(v), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v), GTK_WRAP_WORD_CHAR);
	info = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_pack_start(GTK_BOX(r), dv = diff_view(0, 0), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(r), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(b), c = gtk_button_new_with_label("Commit"), FALSE, FALSE, 0);
	g_signal_connect_swapped(c, "clicked", G_CALLBACK(flip), GINT_TO_POINTER(0));
	gtk_container_set_border_width(GTK_CONTAINER(r), 6);
	pane[0] = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	pane[1] = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_pack1(GTK_PANED(pane[1]), scroll(v), FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[1]), r, TRUE, FALSE);
	gtk_paned_pack1(GTK_PANED(pane[0]), scroll(list), FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[0]), pane[1], TRUE, TRUE);
	load();
	return pane[0];
}

