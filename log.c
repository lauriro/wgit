
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

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
	diff_show(text);
}

int cmd_log(const char *path)
{
	GtkWidget *win, *list, *v, *pane[2];
	GtkTreeViewColumn *c;
	GtkCellRenderer *t = gtk_cell_renderer_text_new();
	GtkListStore *store;
	GtkTreePath *p;
	char *top = repo(path), *m;
	int i, n;
	if (!top) return fputs("wgit: not a git repository\n", stderr), 1;
	win = window(top, "wgit-log");
	row = history(&n);
	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	for (i = 0; i < n; i++) {
		m = row[i].hash ? g_markup_printf_escaped("<span foreground=\"#c55\" font_family=\"monospace\">%s</span> %s  <span foreground=\"#4a4\">%s</span>  <span foreground=\"#39a\">%.16s</span>",
			row[i].hash, row[i].subject, row[i].author, row[i].date) : g_strdup("");
		gtk_list_store_insert_with_values(store, 0, -1, 0, m, 1, i, -1);
		g_free(m);
	}
	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_set(t, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	c = gtk_tree_view_column_new_with_attributes(0, t, "markup", 0, NULL);
	gtk_tree_view_column_set_sizing(c, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_expand(c, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), c);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);
	gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(list), TRUE);
	g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), "changed", G_CALLBACK(pick), 0);
	v = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(v), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(v), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v), GTK_WRAP_WORD_CHAR);
	info = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	pane[0] = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	pane[1] = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_pack1(GTK_PANED(pane[1]), scroll(v), FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[1]), diff_view(0, 0), TRUE, FALSE);
	gtk_paned_pack1(GTK_PANED(pane[0]), scroll(list), FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[0]), pane[1], TRUE, TRUE);
	gtk_paned_set_position(GTK_PANED(pane[0]), 450);
	gtk_paned_set_position(GTK_PANED(pane[1]), 190);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), p = gtk_tree_path_new_first(), 0, FALSE);
	gtk_tree_path_free(p);
	loop(win, pane);
	return 0;
}

