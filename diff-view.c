
#include <gtk/gtk.h>
#include <string.h>
#include "diff-view.h"

static GtkTextBuffer *buf;
static void (*on_hunk)(int from, int to);
static const char *tags[][2] = {
	{"head", "#57c"},
	{"@", "#888"},
	{"+", "#2a2"},
	{"-", "#d33"},
};

static gboolean click(GtkTextView *w, GdkEventButton *ev)
{
	GtkTextIter a, b;
	int x, y;
	if (ev->button == 1 && on_hunk && !gtk_text_buffer_get_selection_bounds(buf, &a, &b)) {
		gtk_text_view_window_to_buffer_coords(w, GTK_TEXT_WINDOW_TEXT, ev->x, ev->y, &x, &y);
		gtk_text_view_get_iter_at_location(w, &a, x, y);
		gtk_text_iter_set_line_offset(&a, 0);
		if (gtk_text_iter_get_char(&a) == '@') on_hunk(gtk_text_iter_get_line(&a), -1);
	}
	return FALSE;
}

GtkWidget *diff_view(void (*hunk)(int from, int to))
{
	GtkWidget *s = gtk_scrolled_window_new(0, 0), *v = gtk_text_view_new();
	int i;
	buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	gtk_text_view_set_editable(GTK_TEXT_VIEW(v), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(v), TRUE);
	for (i = 0; i < 4; i++) gtk_text_buffer_create_tag(buf, tags[i][0], "foreground", tags[i][1], NULL);
	on_hunk = hunk;
	g_signal_connect(v, "button-release-event", G_CALLBACK(click), 0);
	gtk_container_add(GTK_CONTAINER(s), v);
	return s;
}

void diff_show(const char *text)
{
	GtkTextIter it;
	const char *p, *n;
	char *t, s[2] = "";
	int head = 1;
	gtk_text_buffer_set_text(buf, "", 0);
	gtk_text_buffer_get_start_iter(buf, &it);
	for (p = text; p && *p; p = n) {
		n = p + strcspn(p, "\n");
		n += !!*n;
		head &= strncmp(p, "@@", 2) != 0;
		*s = *p;
		t = g_utf8_make_valid(p, n - p);
		gtk_text_buffer_insert_with_tags_by_name(buf, &it, t, -1, head ? "head" : strchr("@+-", *p) ? s : 0, NULL);
		g_free(t);
	}
}

void diff_lines(int *from, int *to)
{
	GtkTextIter a, b;
	gtk_text_buffer_get_selection_bounds(buf, &a, &b);
	*from = gtk_text_iter_get_line(&a);
	*to = gtk_text_iter_get_line(&b) - (gtk_text_iter_starts_line(&b) && !gtk_text_iter_equal(&a, &b));
}

