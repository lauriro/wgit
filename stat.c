
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
	char *o, *s, *path = 0;
	int k = 0;
	while (page[k] != gtk_stack_get_visible_child(GTK_STACK(stack))) k++;
	if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), &m, &row)) gtk_tree_model_get(m, &row, 1, &path, -1);
	s = g_utf8_make_valid(o = stats(days[k], time(0), path && *path ? path : 0), -1);
	g_object_set(where, "label", path && *path ? path : "All files", NULL);
	g_free(path);
	gtk_text_buffer_set_text(buf, s, -1);
	free(o);
	g_free(s);
}

GtkWidget *stat_view(GtkWidget **pane)
{
	GtkWidget *v, *l;
	GtkTreeStore *st;
	GtkTreeIter it, par[64];
	GtkTreePath *tp;
	char *o, *p, *e, *q, c;
	const char *prev = "";
	int k, d = 0, len[64] = {0};
	if (!pane[0]) {
		stack = gtk_stack_new();
		for (k = 0; k < 3; k++) {
			v = g_object_new(GTK_TYPE_TEXT_VIEW, "editable", FALSE, "cursor-visible", FALSE, "monospace", TRUE,
				"left-margin", 8, "top-margin", 8, buf ? "buffer" : NULL, buf, NULL);
			buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
			gtk_stack_add_titled(GTK_STACK(stack), page[k] = scroll(v), name[k], name[k]);
		}
		/* The files of HEAD under their folders; a row holds its name and its path, the first row is the whole repo */
		st = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
		gtk_tree_store_append(st, par, 0);
		gtk_tree_store_set(st, par, 0, "All files", 1, "", -1);
		if (git(&o, 0, (char *[]){"git", "ls-tree", "-r", "--name-only", "HEAD", 0})) *o = 0;
		for (p = o; *p; prev = p, p = e) {
			e = p + strcspn(p, "\n");
			if (*e) *e++ = 0;
			while (d && strncmp(p, prev, len[d])) d--;
			for (; d < 63 && (q = strchr(p + len[d], '/')); *q = c) {
				c = *++q;
				*q = 0;
				gtk_tree_store_append(st, par + d + 1, par + d);
				gtk_tree_store_set(st, par + d + 1, 0, p + len[d], 1, p, -1);
				len[++d] = q - p;
			}
			gtk_tree_store_append(st, &it, par + d);
			gtk_tree_store_set(st, &it, 0, p + len[d], 1, p, -1);
		}
		free(o);
		list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st));
		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1, 0, gtk_cell_renderer_text_new(), "text", 0, NULL);
		g_object_set(list, "headers-visible", FALSE, NULL);
		tp = gtk_tree_model_get_path(GTK_TREE_MODEL(st), par);
		gtk_tree_view_expand_row(GTK_TREE_VIEW(list), tp, FALSE);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), tp, 0, FALSE);
		gtk_tree_path_free(tp);
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
