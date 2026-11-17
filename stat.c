
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "git.h"
#include "ui.h"

static const char name[][11] = {"Last month", "Last year", "All"};
static const int days[] = {30, 365, 0};
static GtkWidget *stack, *page[3], *list, *where;
static GtkTextBuffer *buf;

static void load(void)
{
	GtkTreeModel *m;
	GtkTreeIter row;
	GtkWidget *c = gtk_stack_get_visible_child(GTK_STACK(stack));
	char *o, *s, *path = 0;
	int k = 0;
	/* The stack has no visible page until it is shown; it loads then. */
	if (!c) return;
	while (page[k] != c) k++;
	if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), &m, &row)) gtk_tree_model_get(m, &row, 1, &path, -1);
	s = g_utf8_make_valid(o = stats(days[k], time(0), path && *path ? path : 0), -1);
	g_object_set(where, "label", path && *path ? path : "All files", NULL);
	g_free(path);
	gtk_text_buffer_set_text(buf, s, -1);
	free(o);
	g_free(s);
}

static void set(GtkTreeStore *st, GtkTreeIter *it, char *m, const char *path, const char *icon)
{
	gtk_tree_store_set(st, it, 0, m, 1, path, 2, icon, 3, 1, -1);
	g_free(m);
}

static void act(GtkTreeView *v, GtkTreePath *p)
{
	if (!gtk_tree_view_collapse_row(v, p)) gtk_tree_view_expand_row(v, p, FALSE);
}

/* A click on the arrow of a folder closes or opens it; a click on its name selects it */
static gboolean press(GtkTreeView *v, GdkEventButton *ev)
{
	GtkTreePath *p;
	int x, r = 0;
	if (ev->type == GDK_BUTTON_PRESS && gtk_tree_view_get_path_at_pos(v, ev->x, ev->y, &p, 0, &x, 0)) {
		r = x < 16 * gtk_tree_path_get_depth(p) + 4 && (gtk_tree_view_collapse_row(v, p) || gtk_tree_view_expand_row(v, p, FALSE));
		gtk_tree_path_free(p);
	}
	return r;
}

GtkWidget *stat_view(GtkWidget **pane)
{
	static const char count[] = "%s  <span foreground=\"#888\">%d</span>";
	GtkWidget *v, *l;
	GtkTreeStore *st;
	GtkTreeIter it, par[64];
	GtkTreePath *tp;
	char *o, *p, *e, *q, *s, c;
	const char *prev = "";
	int k, m, n = 0, d = 0, len[64] = {0};
	if (!pane[0]) {
		stack = gtk_stack_new();
		for (k = 0; k < 3; k++) {
			v = g_object_new(GTK_TYPE_TEXT_VIEW, "editable", FALSE, "cursor-visible", FALSE, "monospace", TRUE,
				"left-margin", 8, "top-margin", 8, buf ? "buffer" : NULL, buf, NULL);
			buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
			gtk_stack_add_titled(GTK_STACK(stack), page[k] = scroll(v), name[k], name[k]);
		}
		/* The files of HEAD under their folders. A row holds its markup, its path and its arrow;
		 * the first row is the whole repo. A folder row counts the files below it */
		st = gtk_tree_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
		gtk_tree_store_append(st, par, 0);
		if (git(&o, 0, (char *[]){"git", "ls-tree", "-rz", "--name-only", "HEAD", 0})) *o = 0;
		for (p = o; *p; prev = p, p = e, n++) {
			e = p + strlen(p) + 1;
			while (d && strncmp(p, prev, len[d])) d--;
			for (; d < 63 && (q = strchr(p + len[d], '/')); *q = c) {
				c = *++q;
				m = q - p;
				for (k = 1, s = e; *s && !strncmp(s, p, m); s += strlen(s) + 1) k++;
				*q = 0;
				gtk_tree_store_append(st, par + d + 1, par + d);
				set(st, par + d + 1, g_markup_printf_escaped(count, p + len[d], k), p, "pan-end-symbolic");
				len[++d] = m;
			}
			gtk_tree_store_append(st, &it, par + d);
			set(st, &it, g_markup_printf_escaped("%s", p + len[d]), p, 0);
		}
		free(o);
		set(st, par, g_strdup_printf(count, "<b>All files</b>", n), "", 0);
		list = tree(GTK_TREE_MODEL(st));
		tp = gtk_tree_model_get_path(GTK_TREE_MODEL(st), par);
		gtk_tree_view_expand_row(GTK_TREE_VIEW(list), tp, FALSE);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), tp, 0, FALSE);
		gtk_tree_path_free(tp);
		g_signal_connect(list, "button-press-event", G_CALLBACK(press), 0);
		g_signal_connect(list, "row-activated", G_CALLBACK(act), 0);
		g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), "changed", G_CALLBACK(load), 0);
		g_signal_connect(stack, "notify::visible-child", G_CALLBACK(load), 0);
		pane[0] = split(GTK_ORIENTATION_HORIZONTAL);
		gtk_box_pack_start(GTK_BOX(l = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)), g_object_new(GTK_TYPE_STACK_SWITCHER, "stack", stack, "margin", 6, NULL), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(l), scroll(list), TRUE, TRUE, 0);
		gtk_paned_pack1(GTK_PANED(pane[0]), l, FALSE, FALSE);
		gtk_paned_pack2(GTK_PANED(pane[0]), tabs(stack, where = g_object_new(GTK_TYPE_LABEL, "ellipsize", PANGO_ELLIPSIZE_START, NULL)), TRUE, TRUE);
	}
	load();
	return pane[0];
}

