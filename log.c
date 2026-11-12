
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

static GtkWidget *list, *dv;
static GtkListStore *store;
static GtkTextBuffer *info;
static Row *row;
static char *text;

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
	int i, n;
	gtk_list_store_clear(store);
	row = history(&n);
	for (i = 0; i < n; i++) {
		m = row[i].hash ? g_markup_printf_escaped("<span foreground=\"#c55\" font_family=\"monospace\">%s</span> %s  <span foreground=\"#4a4\">%s</span>  <span foreground=\"#39a\">%.16s</span>",
			row[i].hash, row[i].subject, row[i].author, row[i].date) : g_strdup("");
		gtk_list_store_insert_with_values(store, 0, -1, 0, m, 1, i, -1);
		g_free(m);
	}
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

