
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

static gboolean click(GtkTextView *w, GdkEventButton *ev, Hunk *hunk)
{
	GtkTextIter a, b;
	int l;
	if (ev->button == 1 && !gtk_text_buffer_get_selection_bounds(gtk_text_view_get_buffer(w), &a, &b)) {
		l = line(w, ev, &a);
		if (gtk_text_iter_get_char(&a) == '@') hunk(l, -1);
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
	int i;
	gtk_text_view_set_editable(GTK_TEXT_VIEW(v), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(v), TRUE);
	for (i = 0; i < 4; i++) gtk_text_buffer_create_tag(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v)), tags[i][0], "foreground", tags[i][1], NULL);
	if (hunk) g_signal_connect(v, "button-release-event", G_CALLBACK(click), hunk);
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

int diff_lines(GtkWidget *view, int *from, int *to)
{
	GtkTextIter a, b;
	int r = gtk_text_buffer_get_selection_bounds(buf(view), &a, &b);
	*from = gtk_text_iter_get_line(&a);
	*to = gtk_text_iter_get_line(&b) - (gtk_text_iter_starts_line(&b) && !gtk_text_iter_equal(&a, &b));
	return r;
}

