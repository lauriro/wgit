
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

static GtkWidget *win, *list, *dv, *bar[4], *amend;
static GtkTextBuffer *msg;
static GtkTreeStore *store;
static const char *head[] = {"Staged", "Unstaged", "Untracked"}, *arrow[] = {"pan-down-symbolic", "pan-end-symbolic"};
static const char *verb[] = {"Stage", "Unstage", "Revert", "Ignore"}, *ask[][2] = {
	{"Revert changes in %d file%s?", "_Revert"},
	{"Delete %d untracked file%s?", "_Delete"},
	{"Revert this hunk in %s?", "_Revert"},
	{"Revert these lines in %s?", "_Revert"}
};
static const char use[] = {6, 1, 6, 4};
static Entry *ent;
static int nent, sec, busy, sorted, hunk, one, part, shut[3];
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
	diff_show(dv, text);
	for (k = 0; k < 4; k++) gtk_widget_set_sensitive(bar[k], l && use[k] >> sec & 1);
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

static int sure(char *q, const char *yes)
{
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL, "%s", q);
	int r;
	g_free(q);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "This cannot be undone.");
	gtk_dialog_add_button(GTK_DIALOG(d), yes, GTK_RESPONSE_ACCEPT);
	r = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT;
	gtk_widget_destroy(d);
	return r;
}

static void skip(Entry **e, int n)
{
	GtkWidget *d = gtk_dialog_new_with_buttons("Ignore", GTK_WINDOW(win), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Ignore", GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *c = gtk_dialog_get_content_area(GTK_DIALOG(d)), *t = gtk_text_view_new(), *s = gtk_check_button_new_with_label("Sort .gitignore"), *w;
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t));
	GtkTextIter i, j;
	char *p = patterns(e, n), *o;
	int r;
	gtk_text_buffer_set_text(b, p, -1);
	free(p);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(t), TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s), sorted);
	gtk_box_set_spacing(GTK_BOX(c), 6);
	gtk_container_set_border_width(GTK_CONTAINER(c), 6);
	gtk_box_pack_start(GTK_BOX(c), w = gtk_label_new("Add to .gitignore, one pattern per line:"), FALSE, FALSE, 0);
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_box_pack_start(GTK_BOX(c), w = scroll(t), TRUE, TRUE, 0);
	gtk_widget_set_size_request(w, 400, 120);
	gtk_box_pack_start(GTK_BOX(c), s, FALSE, FALSE, 0);
	gtk_widget_show_all(c);
	r = gtk_dialog_run(GTK_DIALOG(d));
	sorted = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s));
	gtk_text_buffer_get_bounds(b, &i, &j);
	p = gtk_text_buffer_get_text(b, &i, &j, FALSE);
	gtk_widget_destroy(d);
	if (r == GTK_RESPONSE_ACCEPT) done(ignore(&o, g_strstrip(p), sorted), &o);
	g_free(p);
}

static void run(GtkWidget *w, gpointer k)
{
	GtkTreeModel *m;
	GtkTreeIter it;
	GList *l = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), &m), *p;
	Entry **e = malloc(g_list_length(l) * sizeof *e + 1);
	int i, n = 0, op = GPOINTER_TO_INT(k);
	char *o;
	for (p = l; p; p = p->next) {
		gtk_tree_model_get_iter(m, &it, p->data);
		gtk_tree_model_get(m, &it, 1, &i, -1);
		e[n++] = ent + i;
	}
	g_list_free_full(l, (GDestroyNotify)gtk_tree_path_free);
	if (n && op == 3) skip(e, n);
	else if (n && (op < 2 || sure(g_strdup_printf(ask[sec > 1][0], n, n > 1 ? "s" : ""), ask[sec > 1][1]))) done(files(&o, op, e, n), &o);
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

static void all(GtkWidget *w, gpointer k)
{
	GtkTreeSelection *s = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	GtkTreePath *a = gtk_tree_path_new_from_indices(part, -1), *b;
	GtkTreeIter it;
	gtk_tree_view_expand_row(GTK_TREE_VIEW(list), a, FALSE);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, a);
	b = gtk_tree_path_new_from_indices(part, gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), &it) - 1, -1);
	gtk_tree_path_append_index(a, 0);
	gtk_tree_selection_unselect_all(s);
	gtk_tree_selection_select_range(s, a, b);
	gtk_tree_path_free(a);
	gtk_tree_path_free(b);
	run(0, k);
}

static void heads(GtkTreePath *p, GdkEventButton *ev)
{
	static const char *name[] = {"Stage all", "Unstage all", "Revert all", "Ignore all", "Delete all"};
	GtkWidget *m = gtk_menu_new(), *i;
	GtkTreeIter it;
	int k, n;
	part = *gtk_tree_path_get_indices(p);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, p);
	gtk_tree_model_get(GTK_TREE_MODEL(store), &it, 3, &n, -1);
	for (k = 0; k < 4; k++) if (use[k] >> part & 1) {
		i = gtk_menu_item_new_with_label(name[k == 2 && part == 2 ? 4 : k]);
		gtk_widget_set_sensitive(i, n > 0);
		g_signal_connect(i, "activate", G_CALLBACK(all), GINT_TO_POINTER(k));
		gtk_menu_shell_append(GTK_MENU_SHELL(m), i);
	}
	g_signal_connect(m, "selection-done", G_CALLBACK(gtk_widget_destroy), 0);
	gtk_widget_show_all(m);
	gtk_menu_popup_at_pointer(GTK_MENU(m), (GdkEvent *)ev);
}

static gboolean press(GtkTreeView *v, GdkEventButton *ev)
{
	GtkTreePath *p;
	int r = 0;
	if (gtk_tree_view_get_path_at_pos(v, ev->x, ev->y, &p, 0, 0, 0)) {
		if ((r = gtk_tree_path_get_depth(p) < 2) && ev->type == GDK_BUTTON_PRESS) {
			if (ev->button == 3) heads(p, ev);
			else act(v, p);
		}
		gtk_tree_path_free(p);
	}
	return r;
}

static void lines(int from, int to)
{
	char *o;
	if (text) done(apply(&o, text, from, to, !sec), &o);
}

static void pick(GtkWidget *w, gpointer p)
{
	int k = GPOINTER_TO_INT(p), f = hunk, t = -1;
	char *o;
	if (k > 3) {
		context = k > 4 ? MAX(1, context / 2) : 2 * context;
		sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)));
	} else {
		if (k & 1 && !diff_lines(dv, &f, &t)) f = t = one;
		if (k < 2 || sure(g_strdup_printf(ask[k][0], cur), ask[k][1])) done(apply(&o, text, f, t, k < 2 ? !sec : 2), &o);
	}
}

static void menu(GtkMenu *m, int h, int l)
{
	static const char *name[] = {"Stage hunk", "Stage lines", "Revert hunk", "Revert lines", "More context", "Less context", "Unstage hunk", "Unstage lines"};
	GtkWidget *i;
	int f, t, k, n = 0, has = diff_lines(dv, &f, &t) || l >= 0;
	hunk = h;
	one = l;
	for (k = 0; k < 6; k++) {
		if ((k == 2 || k == 3) && sec != 1) continue;
		if (k == 4) gtk_menu_shell_insert(GTK_MENU_SHELL(m), gtk_separator_menu_item_new(), n++);
		i = gtk_menu_item_new_with_label(name[k < 2 && !sec ? k + 6 : k]);
		gtk_widget_set_sensitive(i, k < 4 ? (k & 1 ? has : h >= 0) : k == 4 || context > 1);
		g_signal_connect(i, "activate", G_CALLBACK(pick), GINT_TO_POINTER(k));
		gtk_menu_shell_insert(GTK_MENU_SHELL(m), i, n++);
	}
	gtk_menu_shell_insert(GTK_MENU_SHELL(m), gtk_separator_menu_item_new(), n);
	gtk_widget_show_all(GTK_WIDGET(m));
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
	free(base);
	base = 0;
	if (gtk_toggle_button_get_active(b) && !git(&o, 0, (char *[]){"git", "log", "-1", "--format=%B", 0})) {
		gtk_text_buffer_set_text(msg, g_strchomp(o), -1);
		free(o);
		base = parent();
	}
	reload();
}

static gboolean key(GtkWidget *w, GdkEventKey *ev)
{
	if (ev->keyval == GDK_KEY_F5) reload();
	else if (ev->keyval == GDK_KEY_Delete && gtk_widget_has_focus(list) && gtk_widget_get_sensitive(bar[2])) run(0, GINT_TO_POINTER(2));
	else if ((ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) && ev->state & GDK_CONTROL_MASK) submit();
	else return FALSE;
	return TRUE;
}

int cmd_commit(const char *path)
{
	static const char *end[] = {"Commit", "Rescan", "Cancel"};
	GCallback fn[] = {G_CALLBACK(submit), G_CALLBACK(reload), G_CALLBACK(gtk_window_close)};
	GtkWidget *l, *r, *b, *m, *c, *pane[2];
	GtkTreeSelection *s;
	char *top = repo(path);
	int k;
	if (!top) return fputs("wgit: not a git repository\n", stderr), 1;
	win = window(top, "wgit");
	pane[0] = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	pane[1] = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	l = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
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
	for (k = 0; k < 4; k++) {
		gtk_box_pack_start(GTK_BOX(b), bar[k] = gtk_button_new_with_label(verb[k]), TRUE, TRUE, 0);
		g_signal_connect(bar[k], "clicked", G_CALLBACK(run), GINT_TO_POINTER(k));
	}
	gtk_box_pack_start(GTK_BOX(l), scroll(list), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(r), dv = diff_view(lines, menu), TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(r), 6);
	gtk_paned_pack1(GTK_PANED(pane[1]), r, TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[1]), r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), FALSE, FALSE);
	gtk_box_pack_start(GTK_BOX(r), b = scroll(m = gtk_text_view_new()), TRUE, TRUE, 0);
	gtk_widget_set_size_request(b, -1, 120);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(m), TRUE);
	msg = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m));
	gtk_box_pack_start(GTK_BOX(r), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(b), amend = gtk_check_button_new_with_label("Amend"), FALSE, FALSE, 0);
	for (k = 0; k < 3; k++) {
		gtk_box_pack_end(GTK_BOX(b), c = gtk_button_new_with_label(end[k]), FALSE, FALSE, 0);
		g_signal_connect_swapped(c, "clicked", fn[k], win);
	}
	gtk_container_set_border_width(GTK_CONTAINER(r), 6);
	gtk_paned_pack1(GTK_PANED(pane[0]), l, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[0]), pane[1], TRUE, TRUE);
	gtk_paned_set_position(GTK_PANED(pane[0]), 300);
	g_signal_connect(amend, "toggled", G_CALLBACK(last), 0);
	g_signal_connect(win, "key-press-event", G_CALLBACK(key), 0);
	reload();
	loop(win, pane);
	return 0;
}

