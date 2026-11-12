
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"

static GtkWidget *win, *list[2], *fbtn, *lbtn, *amend;
static GtkTextBuffer *msg;
static Entry *ent;
static int nent, side, busy;
static char *cur, *text;

static Entry *find(void)
{
	int i;
	for (i = 0; cur && i < nent; i++) if (!strcmp(ent[i].path, cur)) return ent + i;
	return 0;
}

static void show(void)
{
	Entry *e = find();
	free(text);
	diff_show(text = e ? diff(e, side) : 0);
	gtk_button_set_label(GTK_BUTTON(fbtn), side ? "Unstage file" : "Stage file");
	gtk_button_set_label(GTK_BUTTON(lbtn), side ? "Unstage lines" : "Stage lines");
}

static void reload(void)
{
	GtkTreeIter it;
	GtkTreePath *tp;
	GtkListStore *s;
	char c, *l;
	int i, k, n, hit[2] = {-1, -1};
	busy = 1;
	ent = status(&nent);
	for (k = 0; k < 2; k++) {
		gtk_list_store_clear(s = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(list[k]))));
		for (i = n = 0; i < nent; i++) {
			if ((c = k ? ent[i].x : ent[i].y) == ' ' || (k && c == '?')) continue;
			if (cur && !strcmp(cur, ent[i].path)) hit[k] = n;
			l = g_strdup_printf("%c  %s", c, ent[i].path);
			gtk_list_store_insert_with_values(s, &it, n++, 0, l, 1, ent[i].path, -1);
			g_free(l);
		}
	}
	if (hit[side] < 0) side = !side;
	if (hit[side] < 0) {
		g_free(cur);
		cur = 0;
	} else {
		tp = gtk_tree_path_new_from_indices(hit[side], -1);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(list[side]), tp, 0, FALSE);
		gtk_tree_path_free(tp);
	}
	busy = 0;
	show();
}

static void done(int r, char **o)
{
	GtkWidget *d;
	if (r && *o) {
		d = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", *o);
		gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);
	}
	free(*o);
	reload();
}

static void sel(GtkTreeSelection *s, gpointer k)
{
	GtkTreeModel *m;
	GtkTreeIter it;
	if (busy || !gtk_tree_selection_get_selected(s, &m, &it)) return;
	g_free(cur);
	gtk_tree_model_get(m, &it, 1, &cur, -1);
	side = GPOINTER_TO_INT(k);
	busy = 1;
	gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(list[!side])));
	busy = 0;
	show();
}

static void file(void)
{
	Entry *e = find();
	char *o;
	if (e) done(stage(&o, e, side), &o);
}

static void lines(int from, int to)
{
	char *o;
	if (text) done(apply(&o, text, from, to, side), &o);
}

static void chosen(void)
{
	int f, t;
	diff_lines(&f, &t);
	lines(f, t);
}

static void submit(void)
{
	GtkTextIter a, b;
	char *s, *o;
	int r;
	gtk_text_buffer_get_bounds(msg, &a, &b);
	s = gtk_text_buffer_get_text(msg, &a, &b, FALSE);
	if (!(r = commit(&o, s, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(amend))))) {
		gtk_text_buffer_set_text(msg, "", 0);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(amend), FALSE);
	}
	g_free(s);
	done(r, &o);
}

static void last(GtkToggleButton *b)
{
	char *o;
	if (gtk_toggle_button_get_active(b) && !git(&o, 0, (char *[]){"git", "log", "-1", "--format=%B", 0})) {
		gtk_text_buffer_set_text(msg, g_strchomp(o), -1);
		free(o);
	}
}

static gboolean key(GtkWidget *w, GdkEventKey *ev)
{
	if (ev->keyval == GDK_KEY_F5) reload();
	else if ((ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) && ev->state & GDK_CONTROL_MASK) submit();
	else return FALSE;
	return TRUE;
}

static GtkWidget *scroll(GtkWidget *w)
{
	GtkWidget *s = gtk_scrolled_window_new(0, 0);
	gtk_container_add(GTK_CONTAINER(s), w);
	return s;
}

int cmd_commit(const char *path)
{
	GtkWidget *h, *l, *r, *b, *m, *c;
	char *top = repo(path);
	int k;
	if (!top) return fputs("wgit: not a git repository\n", stderr), 1;
	setenv("NO_AT_BRIDGE", "1", 0);
	gtk_init(0, 0);
	h = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	l = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), top);
	gtk_window_set_default_size(GTK_WINDOW(win), 1000, 700);
	for (k = 0; k < 2; k++) {
		list[k] = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING)));
		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list[k]), -1, k ? "Staged" : "Unstaged",
			gtk_cell_renderer_text_new(), "text", 0, NULL);
		g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(list[k])), "changed", G_CALLBACK(sel), GINT_TO_POINTER(k));
		g_signal_connect(list[k], "row-activated", G_CALLBACK(file), 0);
		(k ? gtk_paned_add2 : gtk_paned_add1)(GTK_PANED(l), scroll(list[k]));
	}
	gtk_box_pack_start(GTK_BOX(r), diff_view(lines), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(r), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(b), fbtn = gtk_button_new(), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(b), lbtn = gtk_button_new(), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(r), b = scroll(m = gtk_text_view_new()), FALSE, FALSE, 0);
	gtk_widget_set_size_request(b, -1, 120);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(m), TRUE);
	msg = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m));
	gtk_box_pack_start(GTK_BOX(r), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(b), amend = gtk_check_button_new_with_label("Amend"), FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(b), c = gtk_button_new_with_label("Commit"), FALSE, FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(r), 6);
	gtk_paned_pack1(GTK_PANED(h), l, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED(h), r, TRUE, TRUE);
	gtk_paned_set_position(GTK_PANED(h), 300);
	gtk_paned_set_position(GTK_PANED(l), 340);
	gtk_container_add(GTK_CONTAINER(win), h);
	g_signal_connect(fbtn, "clicked", G_CALLBACK(file), 0);
	g_signal_connect(lbtn, "clicked", G_CALLBACK(chosen), 0);
	g_signal_connect(amend, "toggled", G_CALLBACK(last), 0);
	g_signal_connect(c, "clicked", G_CALLBACK(submit), 0);
	g_signal_connect(win, "key-press-event", G_CALLBACK(key), 0);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), 0);
	reload();
	gtk_widget_show_all(win);
	gtk_main();
	return 0;
}

