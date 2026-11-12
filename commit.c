
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"

static GtkWidget *win, *list, *bar[3], *lbtn, *amend;
static GtkTextBuffer *msg;
static GtkTreeStore *store;
static const char *head[] = {"Staged", "Unstaged", "Untracked"}, *arrow[] = {"pan-down-symbolic", "pan-end-symbolic"};
static const char *verb[] = {"Stage", "Unstage", "Revert"}, *ask[][2] = {
	{"Revert changes in %d file%s?", "_Revert"},
	{"Delete %d untracked file%s?", "_Delete"}
};
static Entry *ent;
static int nent, sec, busy, shut[3];
static char *cur, *text;

static void set(GtkTreeIter *it, char *m, int i, const char *icon, int n)
{
	gtk_tree_store_set(store, it, 0, m, 1, i, 2, icon, 3, n, -1);
	g_free(m);
}

static void sel(GtkTreeSelection *s)
{
	GtkTreeModel *m;
	GtkTreeIter it;
	GtkTreePath *p;
	GList *l;
	int k;
	if (busy) return;
	l = gtk_tree_selection_get_selected_rows(s, &m);
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(list), &p, 0);
	g_free(cur);
	free(text);
	cur = text = 0;
	if (l) {
		if (!p || !gtk_tree_selection_path_is_selected(s, p)) {
			gtk_tree_path_free(p);
			p = gtk_tree_path_copy(l->data);
		}
		gtk_tree_model_get_iter(m, &it, p);
		gtk_tree_model_get(m, &it, 1, &k, -1);
		cur = g_strdup(ent[k].path);
		text = diff(ent + k, !(sec = *gtk_tree_path_get_indices(p)));
	}
	diff_show(text);
	gtk_button_set_label(GTK_BUTTON(lbtn), sec ? "Stage lines" : "Unstage lines");
	gtk_widget_set_sensitive(lbtn, !!l);
	for (k = 0; k < 3; k++) gtk_widget_set_sensitive(bar[k], l && (k == 1) == !sec);
	gtk_tree_path_free(p);
	g_list_free_full(l, (GDestroyNotify)gtk_tree_path_free);
}

static void reload(void)
{
	GtkTreeIter it, hd, hit;
	GtkTreePath *tp;
	char c;
	int i, k, n, got = 0;
	busy = 1;
	ent = status(&nent);
	gtk_tree_store_clear(store);
	for (k = 0; k < 3; k++) {
		gtk_tree_store_append(store, &hd, 0);
		for (i = n = 0; i < nent; i++) {
			if ((c = k ? ent[i].y : ent[i].x) == ' ' || (c == '?') != (k == 2)) continue;
			n++;
			gtk_tree_store_append(store, &it, &hd);
			set(&it, g_markup_printf_escaped("%c  %s", c, ent[i].path), i, 0, 0);
			if (!shut[k] && cur && !strcmp(cur, ent[i].path) && (!got || !k == !sec)) hit = it, got = 1;
		}
		set(&hd, g_strdup_printf("<b>%s</b>  <span alpha=\"50%%\">%d</span>", head[k], n), 0, arrow[n && shut[k]], n);
		tp = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &hd);
		if (!shut[k]) gtk_tree_view_expand_row(GTK_TREE_VIEW(list), tp, FALSE);
		gtk_tree_path_free(tp);
	}
	if (got) {
		tp = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &hit);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), tp, 0, FALSE);
		gtk_tree_path_free(tp);
	}
	busy = 0;
	sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)));
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

static gboolean can(GtkTreeSelection *s, GtkTreeModel *m, GtkTreePath *p, gboolean on)
{
	GList *l;
	int r = gtk_tree_path_get_depth(p) > 1;
	if (r && !on && (l = gtk_tree_selection_get_selected_rows(s, 0))) {
		r = *gtk_tree_path_get_indices(l->data) == *gtk_tree_path_get_indices(p);
		g_list_free_full(l, (GDestroyNotify)gtk_tree_path_free);
	}
	return r;
}

static void run(GtkWidget *w, gpointer k)
{
	GtkTreeModel *m;
	GtkTreeIter it;
	GtkWidget *d;
	GList *l = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), &m), *p;
	Entry **e = malloc(g_list_length(l) * sizeof *e + 1);
	int i, n = 0, op = GPOINTER_TO_INT(k), r = GTK_RESPONSE_ACCEPT;
	char *o;
	for (p = l; p; p = p->next) {
		gtk_tree_model_get_iter(m, &it, p->data);
		gtk_tree_model_get(m, &it, 1, &i, -1);
		e[n++] = ent + i;
	}
	g_list_free_full(l, (GDestroyNotify)gtk_tree_path_free);
	if (n && op == 2) {
		d = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL, ask[sec > 1][0], n, n > 1 ? "s" : "");
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "This cannot be undone.");
		gtk_dialog_add_button(GTK_DIALOG(d), ask[sec > 1][1], GTK_RESPONSE_ACCEPT);
		r = gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);
	}
	if (n && r == GTK_RESPONSE_ACCEPT) done(files(&o, op, e, n), &o);
	free(e);
}

static void act(GtkTreeView *v, GtkTreePath *p)
{
	if (gtk_tree_path_get_depth(p) > 1) run(0, GINT_TO_POINTER(!sec));
	else if (!gtk_tree_view_collapse_row(v, p)) gtk_tree_view_expand_row(v, p, FALSE);
}

static void fold(GtkTreeView *v, GtkTreeIter *it, GtkTreePath *p, gpointer c)
{
	gtk_tree_store_set(store, it, 2, arrow[shut[*gtk_tree_path_get_indices(p)] = GPOINTER_TO_INT(c)], -1);
}

static gboolean press(GtkTreeView *v, GdkEventButton *ev)
{
	GtkTreePath *p;
	int r = 0;
	if (gtk_tree_view_get_path_at_pos(v, ev->x, ev->y, &p, 0, 0, 0)) {
		if ((r = gtk_tree_path_get_depth(p) < 2) && ev->type == GDK_BUTTON_PRESS) act(v, p);
		gtk_tree_path_free(p);
	}
	return r;
}

static void lines(int from, int to)
{
	char *o;
	if (text) done(apply(&o, text, from, to, !sec), &o);
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
	GtkTreeSelection *s;
	char *top = repo(path);
	int k;
	if (!top) return fputs("wgit: not a git repository\n", stderr), 1;
	setenv("NO_AT_BRIDGE", "1", 0);
	g_set_prgname("wgit");
	gtk_init(0, 0);
	h = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	l = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), top);
	gtk_window_set_default_size(GTK_WINDOW(win), 1000, 700);
	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store = gtk_tree_store_new(4, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT)));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1, 0, gtk_cell_renderer_pixbuf_new(), "icon-name", 2, "sensitive", 3, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1, 0, gtk_cell_renderer_text_new(), "markup", 0, NULL);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);
	gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(list), FALSE);
	s = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	gtk_tree_selection_set_mode(s, GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_set_select_function(s, (GtkTreeSelectionFunc)can, 0, 0);
	g_signal_connect(s, "changed", G_CALLBACK(sel), 0);
	g_signal_connect(list, "row-activated", G_CALLBACK(act), 0);
	g_signal_connect(list, "row-collapsed", G_CALLBACK(fold), GINT_TO_POINTER(1));
	g_signal_connect(list, "row-expanded", G_CALLBACK(fold), 0);
	g_signal_connect(list, "button-press-event", G_CALLBACK(press), 0);
	gtk_box_pack_start(GTK_BOX(l), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), FALSE, FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(b), 6);
	for (k = 0; k < 3; k++) {
		gtk_box_pack_start(GTK_BOX(b), bar[k] = gtk_button_new_with_label(verb[k]), TRUE, TRUE, 0);
		g_signal_connect(bar[k], "clicked", G_CALLBACK(run), GINT_TO_POINTER(k));
	}
	gtk_box_pack_start(GTK_BOX(l), scroll(list), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(r), diff_view(lines), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(r), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), FALSE, FALSE, 0);
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
	gtk_container_add(GTK_CONTAINER(win), h);
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

