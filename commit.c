
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

static GtkWidget *list, *dv, *bar[4], *amend;
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
static int sec, row, busy, follow, sorted, hunk, part, shown = -1;
/* A section or a folder has the key "\n<section>/<folder path>\n"; shut holds the keys of the closed ones, spot the key
 * of the one under the menu. its has the row of each entry, staged first, then not staged */
static char *cur, *text, *shut, *spot;
static GtkTreeIter *its;

/* A file row has its entry index, a section or a folder has -1 and its key */
static void set(GtkTreeIter *it, char *m, int i, const char *icon, int n, char *key)
{
	gtk_tree_store_set(store, it, 0, m, 1, i, 2, icon, 3, n, 4, key, -1);
	g_free(m);
	g_free(key);
}

static int has(int k, int i)
{
	char c = k ? ent[i].y : ent[i].x;
	return c != ' ' && (c == '?') == (k == 2) ? c : 0;
}

/* The length of the folder part that two paths share. An untracked folder ends with a slash and is a file here */
static int common(const char *a, const char *b)
{
	int i, n = 0;
	for (i = 0; a[i] && a[i] == b[i]; i++) if (a[i] == '/' && a[i + 1]) n = i + 1;
	return n;
}

static int file(GtkTreePath *p)
{
	GtkTreeIter it;
	int k;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, p);
	gtk_tree_model_get(GTK_TREE_MODEL(store), &it, 1, &k, -1);
	return k;
}

/* The files of a closed section or folder are closed in the diff also */
static void hide(const char *key, int on)
{
	if (!busy && key[1] - '0' == shown) diff_fold(dv, key + 3, strcspn(key + 1, "\n") - 2, on);
}

/* A closed folder takes the selection from a file in it. The diff and the place in the list stay then;
 * only a list that was filled again with no file to select empties the diff */
static void sel(GtkTreeSelection *s)
{
	GtkTreePath *p;
	GList *l;
	const char *q;
	int k;
	if (busy) return;
	l = gtk_tree_selection_get_selected_rows(s, 0);
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(list), &p, 0);
	if (l) {
		if (!p || !gtk_tree_selection_path_is_selected(s, p)) {
			gtk_tree_path_free(p);
			p = gtk_tree_path_copy(l->data);
		}
		g_free(cur);
		cur = g_strdup(ent[file(p)].path);
		sec = *gtk_tree_path_get_indices(p);
		for (row = 0, k = file(l->data); k--;) row += !!has(sec, k);
	}
	if (l ? sec != shown : shown < 0) {
		free(text);
		diff_files(dv, text = l ? diffs(sec, ent, nent) : 0);
		shown = l ? sec : -1;
		for (q = shut; *q; q += strcspn(q + 1, "\n") + 2) hide(q, 1);
	}
	if (l && !follow) diff_goto(dv, cur);
	for (k = 0; k < 4; k++) gtk_widget_set_sensitive(bar[k], l && use[k] >> sec & 1);
	gtk_tree_path_free(p);
	g_list_free_full(l, (GDestroyNotify)gtk_tree_path_free);
}

static void top(const char *path)
{
	GtkTreePath *p;
	int k;
	if (shown < 0 || gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(list))) > 1) return;
	for (k = 0; k < nent; k++) {
		if (!has(shown, k) || strcmp(ent[k].path, path)) continue;
		p = gtk_tree_model_get_path(GTK_TREE_MODEL(store), its + 2 * k + !!shown);
		follow = 1;
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), p, 0, FALSE);
		follow = 0;
		gtk_tree_path_free(p);
	}
}

static void fill(void)
{
	/* The selection stays on its file. When that file left, the choice in order is: the file now at its row or the
	 * next one below in the section, the first file of a section below, the last file above in the section.
	 * With no file to keep, the first unstaged file opens, as the next step is to stage; else staged, else untracked */
	static const char rank[] = {1, 0, 2};
	/* The colour of a name tells the usual states: modified and untracked plain, added green, deleted red.
	 * Any other state shows its letter */
	static const char state[] = "M?AD", *fmt[] = {"%s", "%s", "<span foreground=\"#2a2\">%s</span>", "<span foreground=\"#d33\">%s</span>", "%s  <span foreground=\"#888\">%c</span>"};
	GtkTreeIter it, hit, alt, par[64];
	GtkTreePath *tp;
	const char *prev, *s;
	char c, o, *p, *key;
	int i, j, k, n, d, m, q, got = 0, best = 3, len[64], hid[64];
	GdkRectangle r, vr;
	float at = -1;
	busy = 1;
	/* The selected row keeps its place on the screen, so the next file is where the last one was */
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(list), &tp, 0);
	if (tp) {
		gtk_tree_view_get_cell_area(GTK_TREE_VIEW(list), tp, 0, &r);
		gtk_tree_view_get_visible_rect(GTK_TREE_VIEW(list), &vr);
		if (r.height && vr.height > r.height) at = (float)r.y / (vr.height - r.height);
		gtk_tree_path_free(tp);
	}
	gtk_tree_store_clear(store);
	its = realloc(its, (2 * nent + 1) * sizeof *its);
	for (*len = k = 0; k < 3; k++) {
		gtk_tree_store_append(store, par, 0);
		key = g_strdup_printf("\n%d/\n", k);
		*hid = !!strstr(shut, key);
		for (i = n = d = 0, prev = ""; i < nent; i++) {
			if (!(c = has(k, i))) continue;
			n++;
			p = ent[i].path;
			while (d && strncmp(p, prev, len[d])) d--;
			/* A folder gets a row when it has a file or more than one child, so a chain of lone folders is one row */
			while (d < 63 && (m = common(p, p)) > len[d]) {
				for (j = i + 1; j < nent; j++) if (has(k, j)) {
					if ((q = common(p, ent[j].path)) <= len[d]) break;
					if (q < m) m = q;
				}
				for (q = 0, j = i; j < nent && (!has(k, j) || !strncmp(p, ent[j].path, m)); j++) q += !!has(k, j);
				gtk_tree_store_append(store, par + d + 1, par + d);
				o = p[m];
				p[m] = 0;
				s = g_strdup_printf("\n%d/%s\n", k, p);
				hid[d + 1] = hid[d] | (j = !!strstr(shut, s));
				set(par + d + 1, g_markup_printf_escaped("%s  <span foreground=\"#888\">%d</span>", p + len[d], q), -1, arrow[j], q, (char *)s);
				p[m] = o;
				len[++d] = m;
			}
			gtk_tree_store_append(store, &it, par + d);
			its[2 * i + !!k] = it;
			s = strchr(state, c);
			set(&it, g_markup_printf_escaped(fmt[s ? s - state : 4], p + len[d], c), i, 0, 0, 0);
			prev = p;
			if (hid[d]) continue;
			q = !cur ? 0 : k != sec ? 2 * (k > sec) : !strcmp(cur, p) ? 4 : n > row ? 3 : 1;
			if (q > got || (q == 1 && got == 1)) hit = it, got = q;
			if (rank[k] < best) alt = it, best = rank[k];
		}
		/* The count grey, not half transparent: a markup alpha leaked into the diff's text, which drew at half
		 * strength once it had the focus, until the pointer left the window */
		set(par, g_strdup_printf("<b>%s</b>  <span foreground=\"#888\">%d</span>", head[k], n), -1, arrow[n && *hid], n, key);
		tp = gtk_tree_model_get_path(GTK_TREE_MODEL(store), par);
		if (!*hid) gtk_tree_view_expand_row(GTK_TREE_VIEW(list), tp, FALSE);
		gtk_tree_path_free(tp);
	}
	if (got || best < 3) {
		tp = gtk_tree_model_get_path(GTK_TREE_MODEL(store), got ? &hit : &alt);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(list), tp, 0, FALSE);
		if (at >= 0 && at <= 1) gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(list), tp, 0, TRUE, at, 0);
		gtk_tree_path_free(tp);
	}
	busy = 0;
	shown = -1;
	sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)));
}

static void reload(void)
{
	status();
	fill();
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
	int r = file(p) >= 0;
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
	g_object_set(d, "secondary-text", "This cannot be undone.", NULL);
	gtk_dialog_add_button(GTK_DIALOG(d), yes, GTK_RESPONSE_ACCEPT);
	r = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT;
	gtk_widget_destroy(d);
	return r;
}

static void skip(Entry **e, int n)
{
	GtkWidget *d = gtk_dialog_new_with_buttons("Ignore", GTK_WINDOW(win), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Ignore", GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *c = gtk_dialog_get_content_area(GTK_DIALOG(d)), *t = g_object_new(GTK_TYPE_TEXT_VIEW, "monospace", TRUE, NULL), *s = gtk_check_button_new_with_label("Sort .gitignore"), *w;
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t));
	char *p = patterns(e, n), *o;
	int r;
	gtk_text_buffer_set_text(b, p, -1);
	free(p);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s), sorted);
	g_object_set(c, "spacing", 6, "border-width", 6, NULL);
	gtk_box_pack_start(GTK_BOX(c), g_object_new(GTK_TYPE_LABEL, "label", "Add to .gitignore, one pattern per line:", "xalign", 0., NULL), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c), w = scroll(t), TRUE, TRUE, 0);
	gtk_widget_set_size_request(w, 400, 120);
	gtk_box_pack_start(GTK_BOX(c), s, FALSE, FALSE, 0);
	gtk_widget_show_all(c);
	r = gtk_dialog_run(GTK_DIALOG(d));
	sorted = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s));
	g_object_get(b, "text", &p, NULL);
	gtk_widget_destroy(d);
	if (r == GTK_RESPONSE_ACCEPT) done(ignore(&o, g_strstrip(p), sorted), &o);
	g_free(p);
}

static void go(Entry **e, int n, int op, int k)
{
	char *o;
	if (n && op == 3) skip(e, n);
	else if (n && (op < 2 || sure(g_strdup_printf(ask[k > 1][0], n, n > 1 ? "s" : ""), ask[k > 1][1]))) done(files(&o, op, e, n), &o);
	free(e);
}

static void run(GtkWidget *w, gpointer k)
{
	GtkTreeSelection *s = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	GList *l = gtk_tree_selection_get_selected_rows(s, 0), *p;
	Entry **e = malloc(gtk_tree_selection_count_selected_rows(s) * sizeof *e + 1);
	int n = 0;
	for (p = l; p; p = p->next) e[n++] = ent + file(p->data);
	g_list_free_full(l, (GDestroyNotify)gtk_tree_path_free);
	go(e, n, GPOINTER_TO_INT(k), sec);
}

static void act(GtkTreeView *v, GtkTreePath *p)
{
	if (file(p) >= 0) run(0, GINT_TO_POINTER(!sec));
	else if (!gtk_tree_view_collapse_row(v, p)) gtk_tree_view_expand_row(v, p, FALSE);
}

/* GTK forgets which rows under a closed row were open, so an opened row opens its folders that are not in shut */
static void fold(GtkTreeView *v, GtkTreeIter *it, GtkTreePath *p, gpointer c)
{
	GtkTreeModel *m = GTK_TREE_MODEL(store);
	GtkTreeIter ch;
	GtkTreePath *tp;
	char *key, *s;
	int ok;
	gtk_tree_model_get(m, it, 4, &key, -1);
	if ((s = strstr(shut, key))) memmove(s, s + strlen(key), strlen(s + strlen(key)) + 1);
	hide(key, !!c);
	if (c) {
		s = shut;
		shut = g_strdup_printf("%s%s", s, key);
		g_free(s);
	} else for (ok = gtk_tree_model_iter_children(m, &ch, it); ok; ok = gtk_tree_model_iter_next(m, &ch)) {
		g_free(key);
		gtk_tree_model_get(m, &ch, 4, &key, -1);
		if (!key) continue;
		if (strstr(shut, key)) {
			hide(key, 1);
			continue;
		}
		tp = gtk_tree_model_get_path(m, &ch);
		gtk_tree_view_expand_row(v, tp, FALSE);
		gtk_tree_path_free(tp);
	}
	g_free(key);
	gtk_tree_store_set(store, it, 2, arrow[!!c], -1);
}

static void all(GtkWidget *w, gpointer k)
{
	Entry **e = malloc(nent * sizeof *e + 1);
	int i, n = 0, len = strlen(spot) - 4;
	for (i = 0; i < nent; i++) if (has(part, i) && !strncmp(ent[i].path, spot + 3, len)) e[n++] = ent + i;
	go(e, n, GPOINTER_TO_INT(k), part);
}

static void heads(GtkTreePath *p, GdkEventButton *ev)
{
	static const char *name[] = {"Stage all", "Unstage all", "Revert all", "Ignore all", "Delete all"};
	GtkWidget *m = gtk_menu_new(), *i;
	GtkTreeIter it;
	int k, n;
	part = *gtk_tree_path_get_indices(p);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, p);
	g_free(spot);
	gtk_tree_model_get(GTK_TREE_MODEL(store), &it, 3, &n, 4, &spot, -1);
	for (k = 0; k < 4; k++) if (use[k] >> part & 1) {
		i = gtk_menu_item_new_with_label(name[k == 2 && part == 2 ? 4 : k]);
		gtk_widget_set_sensitive(i, n > 0);
		g_signal_connect(i, "activate", G_CALLBACK(all), GINT_TO_POINTER(k));
		gtk_menu_shell_insert(GTK_MENU_SHELL(m), i, -1);
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
		if ((r = file(p) < 0) && ev->type == GDK_BUTTON_PRESS) {
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
		shown = -1;
		sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)));
	} else {
		if (k & 1) diff_lines(dv, &f, &t);
		if (k < 2 || sure(g_strdup_printf(ask[k][0], cur), ask[k][1])) done(apply(&o, text, f, t, k < 2 ? !sec : 2), &o);
	}
}

static void menu(GtkMenu *m, int h)
{
	static const char *name[] = {"Stage hunk", "Stage lines", "Revert hunk", "Revert lines", "More context", "Less context", "Unstage hunk", "Unstage lines"};
	GtkWidget *i;
	int f, t, k, n = 0, has = diff_lines(dv, &f, &t);
	hunk = h;
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
	char *s, *o;
	int r;
	g_object_get(msg, "text", &s, NULL);
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
	if (!gtk_widget_get_mapped(list)) return FALSE;
	if (ev->keyval == GDK_KEY_F5) reload();
	else if (ev->keyval == GDK_KEY_Delete && gtk_widget_has_focus(list) && gtk_widget_get_sensitive(bar[2])) run(0, GINT_TO_POINTER(2));
	else if ((ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) && ev->state & GDK_CONTROL_MASK) submit();
	else return FALSE;
	return TRUE;
}

GtkWidget *commit_view(GtkWidget **pane)
{
	static const char *end[] = {"Commit", "Rescan", "Cancel"};
	GCallback fn[] = {G_CALLBACK(submit), G_CALLBACK(reload), G_CALLBACK(gtk_window_close)};
	GtkWidget *l, *r, *b, *m, *c;
	GtkTreeViewColumn *col;
	GtkCellRenderer *name;
	GtkTreeSelection *s;
	int k;
	if (pane[0]) {
		reload();
		return pane[0];
	}
	pane[0] = split(GTK_ORIENTATION_HORIZONTAL);
	pane[1] = split(GTK_ORIENTATION_VERTICAL);
	l = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store = gtk_tree_store_new(5, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING)));
	shut = g_strdup("");
	/* The arrow and the name in one column, so the name follows the indentation of its level */
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), col = gtk_tree_view_column_new());
	gtk_tree_view_column_pack_start(col, name = g_object_new(GTK_TYPE_CELL_RENDERER_PIXBUF, "width", 20, "xpad", 4, "xalign", 0., NULL), FALSE);
	gtk_tree_view_column_set_attributes(col, name, "icon-name", 2, "sensitive", 3, NULL);
	gtk_tree_view_column_pack_start(col, name = gtk_cell_renderer_text_new(), TRUE);
	gtk_tree_view_column_set_attributes(col, name, "markup", 0, NULL);
	g_object_set(list, "headers-visible", FALSE, "show-expanders", FALSE, "level-indentation", 16, NULL);
	s = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	g_object_set(s, "mode", GTK_SELECTION_MULTIPLE, NULL);
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
	gtk_paned_pack1(GTK_PANED(pane[1]), gtk_widget_get_parent(dv = diff_view(lines, menu, top)), TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[1]), r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), FALSE, FALSE);
	gtk_box_pack_start(GTK_BOX(r), b = scroll(m = g_object_new(GTK_TYPE_TEXT_VIEW, "monospace", TRUE, NULL)), TRUE, TRUE, 0);
	gtk_widget_set_size_request(b, -1, 120);
	msg = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m));
	gtk_box_pack_start(GTK_BOX(r), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(b), amend = gtk_check_button_new_with_label("Amend"), FALSE, FALSE, 0);
	for (k = 0; k < 3; k++) {
		gtk_box_pack_end(GTK_BOX(b), c = gtk_button_new_with_label(end[k]), FALSE, FALSE, 0);
		g_signal_connect_swapped(c, "clicked", fn[k], win);
	}
	gtk_container_set_border_width(GTK_CONTAINER(r), 6);
	gtk_paned_pack1(GTK_PANED(pane[0]), l, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(pane[0]), tabs(pane[1], diff_tools(dv)), TRUE, TRUE);
	g_signal_connect(amend, "toggled", G_CALLBACK(last), 0);
	g_signal_connect(win, "key-press-event", G_CALLBACK(key), 0);
	if (!nent) status();
	fill();
	return pane[0];
}

