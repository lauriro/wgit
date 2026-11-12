
#include <gtk/gtk.h>
#include <string.h>
#include "diff-view.h"

typedef void Hunk(int from, int to);
typedef void Menu(GtkMenu *m, int hunk, int line);

static int at = -1, one = -1;
static const char *tags[][2] = {
	{"head", "#57c"},
	{"@", "#888"},
	{"+", "#2a2"},
	{"-", "#d33"},
};

static GtkTextBuffer *buf(GtkWidget *s)
{
	return gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_bin_get_child(GTK_BIN(s))));
}

static int line(GtkTextView *w, GdkEventButton *ev, GtkTextIter *a)
{
	int x, y;
	gtk_text_view_window_to_buffer_coords(w, GTK_TEXT_WINDOW_TEXT, ev->x, ev->y, &x, &y);
	gtk_text_view_get_iter_at_location(w, a, x, y);
	gtk_text_iter_set_line_offset(a, 0);
	return gtk_text_iter_get_line(a);
}

static void fold(GtkTextBuffer *b, int l, int hide)
{
	GtkTextTagTable *t = gtk_text_buffer_get_tag_table(b);
	GtkTextTag *title = gtk_text_tag_table_lookup(t, "title"), *h = gtk_text_tag_table_lookup(t, "hide");
	GtkTextIter s, e;
	gtk_text_buffer_get_iter_at_line(b, &s, l + 1);
	e = s;
	gtk_text_iter_forward_to_tag_toggle(&e, title);
	if (hide < 0) hide = !gtk_text_iter_has_tag(&s, h);
	(hide ? gtk_text_buffer_apply_tag : gtk_text_buffer_remove_tag)(b, h, &s, &e);
	gtk_text_buffer_get_iter_at_line(b, &s, l);
	e = s;
	gtk_text_iter_forward_char(&e);
	gtk_text_buffer_delete(b, &s, &e);
	gtk_text_buffer_insert_with_tags(b, &s, hide ? "▸" : "▾", -1, title, NULL);
}

static gboolean click(GtkTextView *w, GdkEventButton *ev, Hunk *hunk)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(w);
	GtkTextIter a, c;
	int l;
	if (ev->button == 1 && !gtk_text_buffer_get_selection_bounds(b, &a, &c)) {
		l = line(w, ev, &a);
		if (hunk && gtk_text_iter_get_char(&a) == '@') hunk(l, -1);
		if (gtk_text_iter_has_tag(&a, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(b), "title"))) fold(b, l, -1);
	}
	return FALSE;
}

static gboolean press(GtkTextView *w, GdkEventButton *ev)
{
	GtkTextIter a;
	gunichar c;
	if (ev->button == 3) {
		one = at = line(w, ev, &a);
		c = gtk_text_iter_get_char(&a);
		for (; at >= 0 && gtk_text_iter_get_char(&a) != '@'; gtk_text_iter_backward_line(&a)) at--;
		if (at < 0 || (c != '+' && c != '-')) one = -1;
	}
	return FALSE;
}

static void popup(GtkTextView *w, GtkWidget *m, Menu *menu)
{
	if (GTK_IS_MENU(m)) menu(GTK_MENU(m), at, one);
}

GtkWidget *diff_view(Hunk *hunk, Menu *menu)
{
	GtkWidget *s = gtk_scrolled_window_new(0, 0), *v = gtk_text_view_new();
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	int i;
	gtk_text_view_set_editable(GTK_TEXT_VIEW(v), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(v), TRUE);
	for (i = 0; i < 4; i++) gtk_text_buffer_create_tag(b, tags[i][0], "foreground", tags[i][1], NULL);
	gtk_text_buffer_create_tag(b, "title", "weight", PANGO_WEIGHT_BOLD, "paragraph-background", "rgba(128,128,128,0.2)", NULL);
	gtk_text_buffer_create_tag(b, "hide", "invisible", TRUE, NULL);
	g_signal_connect(v, "button-release-event", G_CALLBACK(click), hunk);
	if (menu) {
		g_signal_connect(v, "button-press-event", G_CALLBACK(press), 0);
		g_signal_connect(v, "populate-popup", G_CALLBACK(popup), menu);
	}
	gtk_container_add(GTK_CONTAINER(s), v);
	return s;
}

void diff_show(GtkWidget *view, const char *text)
{
	GtkTextBuffer *b = buf(view);
	GtkTextIter it;
	const char *p, *n;
	char *t, s[2] = "";
	int head = 1;
	gtk_text_buffer_set_text(b, "", 0);
	gtk_text_buffer_get_start_iter(b, &it);
	for (p = text; p && *p; p = n) {
		n = p + strcspn(p, "\n");
		n += !!*n;
		head &= strncmp(p, "@@", 2) != 0;
		*s = *p;
		t = g_utf8_make_valid(p, n - p);
		gtk_text_buffer_insert_with_tags_by_name(b, &it, t, -1, head ? "head" : strchr("@+-", *p) ? s : 0, NULL);
		g_free(t);
	}
}

static void title(GtkTextBuffer *b, GtkTextMark *m, const char *name, int len, int a, int d)
{
	GtkTextIter it;
	char *t = g_utf8_make_valid(name, len), s[32];
	gtk_text_buffer_get_iter_at_mark(b, &it, m);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, "▾ ", -1, "title", NULL);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, t, -1, "title", NULL);
	snprintf(s, sizeof s, "  +%d", a);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, s, -1, "title", "+", NULL);
	snprintf(s, sizeof s, " −%d\n", d);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, s, -1, "title", "-", NULL);
	gtk_text_buffer_delete_mark(b, m);
	g_free(t);
}

int diff_files(GtkWidget *view, const char *text, int *add, int *del)
{
	GtkTextBuffer *b = buf(view);
	GtkTextMark *m = 0;
	GtkTextIter it;
	const char *p, *e, *n, *name = 0;
	char *t, s[2] = "";
	int files = 0, hdr = 0, len = 0, a = 0, d = 0, l;
	*add = *del = 0;
	gtk_text_buffer_set_text(b, "", 0);
	gtk_text_buffer_get_start_iter(b, &it);
	for (p = text; p && *p; p = n) {
		e = p + strcspn(p, "\n");
		n = e + !!*e;
		if (!strncmp(p, "diff --git ", 11)) {
			if (m) title(b, m, name, len, a, d);
			gtk_text_buffer_get_end_iter(b, &it);
			m = gtk_text_buffer_create_mark(b, 0, &it, TRUE);
			l = (e - p - 12) / 2;
			name = p + l + 14;
			len = e - name;
			hdr = 1;
			a = d = 0;
			files++;
			continue;
		}
		if (hdr && !strncmp(p, "+++ b/", 6)) name = p + 6, len = e - name;
		if (*p == '@') hdr = 0;
		if (hdr && (!strncmp(p, "index ", 6) || !strncmp(p, "--- ", 4) || !strncmp(p, "+++ ", 4))) continue;
		if (!hdr) a += *p == '+', d += *p == '-';
		*s = *p;
		t = g_utf8_make_valid(p, n - p);
		gtk_text_buffer_insert_with_tags_by_name(b, &it, t, -1, hdr ? "head" : strchr("@+-", *p) ? s : 0, NULL);
		g_free(t);
		*add += !hdr && *p == '+';
		*del += !hdr && *p == '-';
	}
	if (m) title(b, m, name, len, a, d);
	return files;
}

void diff_fold(GtkWidget *view, int hide)
{
	GtkTextBuffer *b = buf(view);
	GtkTextIter it;
	int l, n = gtk_text_buffer_get_line_count(b);
	for (l = 0; l < n; l++) {
		gtk_text_buffer_get_iter_at_line(b, &it, l);
		if (gtk_text_iter_has_tag(&it, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(b), "title"))) fold(b, l, hide);
	}
}

int diff_lines(GtkWidget *view, int *from, int *to)
{
	GtkTextIter a, b;
	int r = gtk_text_buffer_get_selection_bounds(buf(view), &a, &b);
	*from = gtk_text_iter_get_line(&a);
	*to = gtk_text_iter_get_line(&b) - (gtk_text_iter_starts_line(&b) && !gtk_text_iter_equal(&a, &b));
	return r;
}

