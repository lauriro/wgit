
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "git.h"
#include "ui.h"

static GtkWidget *list, *dv, *bar[4], *amend, *pop;
static GMenu *pops;
static GtkTextBuffer *msg;
static GtkTreeStore *store;
static const char *head[] = {"Staged", "Unstaged", "Untracked"}, *arrow[] = {"pan-down-symbolic", "pan-end-symbolic"};
static const char *verb[] = {"Stage", "Unstage", "Revert", "Ignore"}, *ask[][2] = {
	{"Revert changes in %d file%s?", "_Revert"},
	{"Delete %d untracked file%s?", "_Delete"},
	{"Revert this hunk?", "_Revert"},
	{"Revert these lines?", "_Revert"}
};
static const char use[] = {6, 1, 6, 4};
static int sec, row, busy, follow, sorted, hunk, part, from, to, shown = -1;
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
static void sel(GtkTreeSelection *s, gpointer redo)
{
	GtkTreePath *p, *c;
	GList *l;
	const char *q;
	int k, redid = 0;
	char *was;
	if (busy) return;
	was = g_strdup(cur);
	l = gtk_tree_selection_get_selected_rows(s, 0);
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(list), &p, 0);
	if (l) {
		c = p && gtk_tree_selection_path_is_selected(s, p) ? p : l->data;
		g_free(cur);
		cur = g_strdup(ent[file(c)].path);
		sec = *gtk_tree_path_get_indices(c);
		for (row = 0, k = file(l->data); k--;) row += !!has(sec, k);
	}
	if (redo || (l ? sec != shown : shown < 0)) {
		if (l) shown = sec;
		free(text);
		diff_files(dv, text = shown < 0 ? 0 : diffs(shown, ent, nent));
		redid = 1;
		for (q = shut; *q; q += strcspn(q + 1, "\n") + 2) hide(q, 1);
	}
	/* To the file when it is another one or its diff is new: the list says changed again after a refill */
	if (l && !follow && (redid || !was || strcmp(was, cur))) diff_goto(dv, cur);
	g_free(was);
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
	sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), 0);
}

static void reload(void)
{
	status();
	fill();
}

/* The list reloads at once, the error stays open over it */
static void done(int r, char **o)
{
	if (r && *o) alert(*o);
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

/* A question is answered later: the job waits in job, njob, jop or jfrom, jto. The dialogs are modal, so there is
 * one at a time */
static Entry **job;
static int njob, jop, jfrom, jto;

static void answered(GObject *d, GAsyncResult *r, gpointer fn)
{
	if (gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(d), r, 0) == 1) ((void (*)(void))fn)();
}

static void sure(char *q, const char *yes, void (*fn)(void))
{
	GtkAlertDialog *d = gtk_alert_dialog_new("%s", q);
	g_free(q);
	g_object_set(d, "detail", "This cannot be undone.", "buttons", (const char *[]){"_Cancel", yes, 0}, "cancel-button", 0, NULL);
	gtk_alert_dialog_choose(d, GTK_WINDOW(win), 0, answered, fn);
	g_object_unref(d);
}

static GtkWidget *sorter;
static GtkTextBuffer *pats;

/* Sort is kept however the dialog ends, as GTK 3's dialog did */
static gboolean skipped(GtkWidget *d, gpointer ok)
{
	char *p, *o;
	sorted = gtk_check_button_get_active(GTK_CHECK_BUTTON(sorter));
	if (!ok) return FALSE;
	g_object_get(pats, "text", &p, NULL);
	gtk_window_destroy(GTK_WINDOW(d));
	done(ignore(&o, g_strstrip(p), sorted), &o);
	g_free(p);
	return FALSE;
}

static void skip(Entry **e, int n)
{
	GtkWidget *d = g_object_new(GTK_TYPE_WINDOW, "title", "Ignore", "transient-for", win, "modal", TRUE, NULL),
		*c = margin(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6)), *t = g_object_new(GTK_TYPE_TEXT_VIEW, "monospace", TRUE, NULL),
		*h = g_object_new(GTK_TYPE_BOX, "spacing", 6, "halign", GTK_ALIGN_END, NULL), *w;
	GtkEventController *k = gtk_shortcut_controller_new();
	char *p = patterns(e, n);
	gtk_text_buffer_set_text(pats = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t)), p, -1);
	free(p);
	sorter = gtk_check_button_new_with_label("Sort .gitignore");
	gtk_check_button_set_active(GTK_CHECK_BUTTON(sorter), sorted);
	gtk_box_append(GTK_BOX(c), g_object_new(GTK_TYPE_LABEL, "label", "Add to .gitignore, one pattern per line:", "xalign", 0., NULL));
	gtk_box_append(GTK_BOX(c), w = scroll(t));
	gtk_widget_set_size_request(w, 400, 120);
	gtk_box_append(GTK_BOX(c), sorter);
	gtk_box_append(GTK_BOX(h), w = gtk_button_new_with_mnemonic("_Cancel"));
	g_signal_connect_swapped(w, "clicked", G_CALLBACK(gtk_window_close), d);
	gtk_box_append(GTK_BOX(h), w = gtk_button_new_with_mnemonic("_Ignore"));
	g_signal_connect_swapped(w, "clicked", G_CALLBACK(skipped), d);
	gtk_box_append(GTK_BOX(c), h);
	g_signal_connect(d, "close-request", G_CALLBACK(skipped), 0);
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(k), gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape, 0), gtk_named_action_new("window.close")));
	gtk_widget_add_controller(d, k);
	gtk_window_set_child(GTK_WINDOW(d), c);
	gtk_window_present(GTK_WINDOW(d));
}

static void filed(void)
{
	char *o;
	done(files(&o, jop, job, njob), &o);
}

static void go(Entry **e, int n, int op, int k)
{
	free(job);
	job = e, njob = n, jop = op;
	if (n && op == 3) skip(e, n);
	else if (n && op > 1) sure(g_strdup_printf(ask[k > 1][0], n, n > 1 ? "s" : ""), ask[k > 1][1], filed);
	else if (n) filed();
}

static void run(gpointer k)
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
	if (file(p) >= 0) run(GINT_TO_POINTER(!sec));
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
}

static void all(gpointer k)
{
	Entry **e = malloc(nent * sizeof *e + 1);
	int i, n = 0, len = strlen(spot) - 4;
	for (i = 0; i < nent; i++) if (has(part, i) && !strncmp(ent[i].path, spot + 3, len)) e[n++] = ent + i;
	go(e, n, GPOINTER_TO_INT(k), part);
}

/* A file has the menu of its section without "all": it acts on the selected files, as the buttons do */
/* The popover's parent is the list's box: a tree view does not place a popover */
static void heads(GtkTreePath *p, GdkEvent *ev)
{
	GtkTreeIter it;
	double x, y;
	char *s, *a;
	int k, n, f = file(p) >= 0;
	part = *gtk_tree_path_get_indices(p);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, p);
	g_free(spot);
	gtk_tree_model_get(GTK_TREE_MODEL(store), &it, 3, &n, 4, &spot, -1);
	g_menu_remove_all(pops);
	for (k = 0; k < 4; k++) if (use[k] >> part & 1) {
		s = g_strdup_printf("%s%s", k == 2 && part == 2 ? "Delete" : verb[k], &" all"[4 * f]);
		a = g_strdup_printf("v.%s(%d)", f ? "run" : n > 0 ? "all" : "no", k);
		g_menu_append(pops, s, a);
		g_free(a);
		g_free(s);
	}
	local(gtk_widget_get_parent(pop), ev, &x, &y);
	gtk_popover_set_pointing_to(GTK_POPOVER(pop), &(GdkRectangle){x, y, 1, 1});
	gtk_popover_popup(GTK_POPOVER(pop));
}

static gboolean press(GtkEventController *c, GdkEvent *ev)
{
	GtkTreeView *v = GTK_TREE_VIEW(list);
	GtkTreePath *p;
	double x, y;
	int bx, by, r = 0;
	if (gdk_event_get_event_type(ev) != GDK_BUTTON_PRESS) return FALSE;
	local(list, ev, &x, &y);
	gtk_tree_view_convert_widget_to_bin_window_coords(v, x, y, &bx, &by);
	if (gtk_tree_view_get_path_at_pos(v, bx, by, &p, 0, 0, 0)) {
		r = file(p) < 0;
		if (gdk_button_event_get_button(ev) == 3) {
			if (!r && !gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(v), p)) gtk_tree_view_set_cursor(v, p, 0, FALSE);
			heads(p, ev);
			r = 1;
		} else if (r) act(v, p);
		gtk_tree_path_free(p);
	}
	return r;
}

static void lines(int from, int to)
{
	char *o;
	if (text) done(apply(&o, text, from, to, !sec), &o);
}

static void reverted(void)
{
	char *o;
	done(apply(&o, text, jfrom, jto, 2), &o);
}

static void pick(gpointer p)
{
	int k = GPOINTER_TO_INT(p);
	char *o;
	jfrom = k & 1 ? from : hunk, jto = k & 1 ? to : -1;
	if (k > 3) {
		context = k > 4 ? MAX(1, context / 2) : 2 * context;
		sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), list);
	} else if (k < 2) done(apply(&o, text, jfrom, jto, !sec), &o);
	else sure(g_strdup(ask[k][0]), ask[k][1], reverted);
}

/* Lines of the work tree edited in the diff: one block at a time, saved by hand. The lines' place in the diff is kept,
 * a save reads it from the diff that was shown */
static int efrom, eto;

static int editing(void)
{
	return gtk_text_view_get_editable(GTK_TEXT_VIEW(dv));
}

static void edit(void)
{
	efrom = from, eto = to;
	diff_edit(dv, from, to);
}

/* A save that fails keeps the block, so nothing typed is lost */
static void save(void)
{
	char *s = diff_edited(dv), *why;
	if (rewrite(text, efrom, eto, s, !sec, &why)) {
		alert(why);
		free(why);
	} else {
		/* Where the edit was stays in view: no jump to the file */
		double top = diff_top(dv);
		follow = 1, reload(), follow = 0;
		diff_keep(dv, top);
	}
	g_free(s);
}

static void cancel(void)
{
	double top = diff_top(dv);
	follow = 1, sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), list), follow = 0;
	diff_keep(dv, top);
}

/* The groups are sections, which the menu parts with lines */
static void menu(GMenu *m, int h)
{
	static const char *name[] = {"Stage hunk", "Stage lines", "Revert hunk", "Revert lines", "More context", "Less context", "Unstage hunk", "Unstage lines"};
	GMenu *s = g_menu_new();
	char *why = 0, a[16];
	int k, has = diff_lines(dv, &from, &to);
	hunk = h;
	if (editing()) {
		g_menu_append(s, "Save lines", "v.save(0)");
		g_menu_append(s, "Cancel edit", "v.cancel(0)");
	} else {
		for (k = 0; k < 6; k++) {
			if ((k == 2 || k == 3) && sec != 1) continue;
			if (k == 4) g_menu_append_section(m, 0, G_MENU_MODEL(s)), g_object_unref(s), s = g_menu_new();
			snprintf(a, sizeof a, "v.%s(%d)", (k < 4 ? (k & 1 ? has : h >= 0) : k == 4 || context > 1) ? "pick" : "no", k);
			g_menu_append(s, name[k < 2 && !sec ? k + 6 : k], a);
		}
		/* Work tree or staged lines: a staged edit is staged */
		g_menu_append(s, "Edit lines", has && !rewrite(text, from, to, 0, !sec, &why) ? "v.edit(0)" : "v.no(0)");
		free(why);
	}
	g_menu_append_section(m, 0, G_MENU_MODEL(s));
	g_object_unref(s);
}

static void submit(void)
{
	char *s, *o;
	int r, a = gtk_check_button_get_active(GTK_CHECK_BUTTON(amend));
	g_object_get(msg, "text", &s, NULL);
	if (!(r = commit(&o, s, a))) gtk_text_buffer_set_text(msg, "", 0), keep("");
	g_free(s);
	/* The end of amend reloads the list itself */
	if (r || !a) done(r, &o);
	else {
		free(o);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(amend), FALSE);
	}
}

/* The message goes on at the next start, as in git gui; an amend's is the last commit's, not a new one */
static gboolean closing(void)
{
	char *s;
	if (gtk_check_button_get_active(GTK_CHECK_BUTTON(amend))) return FALSE;
	g_object_get(msg, "text", &s, NULL);
	keep(s);
	g_free(s);
	return FALSE;
}

static void last(GtkCheckButton *b)
{
	char *o;
	free(base);
	base = 0;
	if (gtk_check_button_get_active(b) && !git(&o, 0, (char *[]){"git", "log", "-1", "--format=%B", 0})) {
		gtk_text_buffer_set_text(msg, g_strchomp(o), -1);
		free(o);
		base = parent();
	}
	reload();
}

/* All changed files of the work tree, not the untracked ones, staged: git citool's Ctrl+I */
static void changed(void)
{
	Entry **e = malloc(nent * sizeof *e + 1);
	int i, n = 0;
	for (i = 0; i < nent; i++) if (has(1, i)) e[n++] = ent + i;
	go(e, n, 0, 1);
}

/* In the capture phase, so the window sees a key before the focused widget, as GTK 3's key-press-event on it did */
static gboolean key(GtkEventControllerKey *c, guint k, guint code, GdkModifierType m)
{
	int ctl = m & GDK_CONTROL_MASK;
	if (!gtk_widget_get_mapped(list)) return FALSE;
	if (editing() && k == GDK_KEY_Escape) cancel();
	else if (editing() && k == GDK_KEY_s && ctl) save();
	else if (k == GDK_KEY_F5) reload();
	/* As git citool: Ctrl+T stages, Ctrl+U unstages the selected files, where the buttons can */
	else if ((k == GDK_KEY_t || k == GDK_KEY_u) && ctl) {
		if (gtk_widget_get_sensitive(bar[k == GDK_KEY_u])) run(GINT_TO_POINTER(k == GDK_KEY_u));
	} else if (k == GDK_KEY_i && ctl) changed();
	else if (k == GDK_KEY_Delete && gtk_widget_has_focus(list) && gtk_widget_get_sensitive(bar[2])) run(GINT_TO_POINTER(2));
	else if ((k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) && ctl) submit();
	else return FALSE;
	return TRUE;
}

GtkWidget *commit_view(GtkWidget **pane)
{
	static const char *end[] = {"Rescan", "Commit"}, *name[] = {"pick", "edit", "save", "cancel", "run", "all"};
	static const GCallback does[] = {G_CALLBACK(pick), G_CALLBACK(edit), G_CALLBACK(save), G_CALLBACK(cancel), G_CALLBACK(run), G_CALLBACK(all)};
	GCallback fn[] = {G_CALLBACK(reload), G_CALLBACK(submit)};
	GtkWidget *l, *r, *b, *m, *c;
	GtkEventController *e;
	GtkTreeSelection *s;
	char *o;
	int k;
	if (pane[0]) {
		reload();
		return pane[0];
	}
	l = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	list = tree(GTK_TREE_MODEL(store = gtk_tree_store_new(5, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING)));
	shut = g_strdup("");
	s = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	g_object_set(s, "mode", GTK_SELECTION_MULTIPLE, NULL);
	gtk_tree_selection_set_select_function(s, (GtkTreeSelectionFunc)can, 0, 0);
	g_signal_connect(s, "changed", G_CALLBACK(sel), 0);
	g_signal_connect(list, "row-activated", G_CALLBACK(act), 0);
	g_signal_connect(list, "row-collapsed", G_CALLBACK(fold), GINT_TO_POINTER(1));
	g_signal_connect(list, "row-expanded", G_CALLBACK(fold), 0);
	events(list, G_CALLBACK(press), 0);
	gtk_box_append(GTK_BOX(l), b = margin(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6)));
	for (k = 0; k < 4; k++) {
		gtk_box_append(GTK_BOX(b), bar[k] = g_object_new(GTK_TYPE_BUTTON, "label", verb[k], "hexpand", TRUE, NULL));
		g_signal_connect_swapped(bar[k], "clicked", G_CALLBACK(run), GINT_TO_POINTER(k));
	}
	gtk_box_append(GTK_BOX(l), scroll(list));
	g_object_set(pop = gtk_popover_menu_new_from_model(G_MENU_MODEL(pops = g_menu_new())), "has-arrow", FALSE, "halign", GTK_ALIGN_START, NULL);
	gtk_widget_set_parent(pop, l);
	r = margin(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
	gtk_box_append(GTK_BOX(r), b = scroll(m = g_object_new(GTK_TYPE_TEXT_VIEW, "name", "plain", "monospace", TRUE, NULL)));
	gtk_widget_set_size_request(b, -1, 120);
	msg = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m));
	if ((o = draft())) gtk_text_buffer_set_text(msg, o, -1), free(o);
	gtk_box_append(GTK_BOX(r), b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
	gtk_box_append(GTK_BOX(b), amend = gtk_check_button_new_with_label("Amend"));
	for (k = 0; k < 2; k++) {
		gtk_box_append(GTK_BOX(b), c = g_object_new(GTK_TYPE_BUTTON, "label", end[k], "hexpand", !k, "halign", k ? GTK_ALIGN_FILL : GTK_ALIGN_END, NULL));
		g_signal_connect_swapped(c, "clicked", fn[k], win);
	}
	pane[1] = split(GTK_ORIENTATION_VERTICAL, gtk_widget_get_parent(dv = diff_view(lines, menu, top)), r, 1);
	pane[0] = split(GTK_ORIENTATION_HORIZONTAL, l, tabs(pane[1], diff_tools(dv)), 0);
	actions(pane[0], name, does, G_N_ELEMENTS(name));
	g_signal_connect(amend, "toggled", G_CALLBACK(last), 0);
	g_signal_connect(win, "close-request", G_CALLBACK(closing), 0);
	e = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(e, GTK_PHASE_CAPTURE);
	g_signal_connect(e, "key-pressed", G_CALLBACK(key), 0);
	gtk_widget_add_controller(win, e);
	if (!nent) status();
	fill();
	return pane[0];
}

