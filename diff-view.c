
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

static int map(GtkWidget *w, int l)
{
	GArray *m = g_object_get_data(G_OBJECT(w), "map");
	return l < 0 || l >= (int)m->len ? l : g_array_index(m, int, l);
}

static int line(GtkTextView *w, GdkEventButton *ev, GtkTextIter *a)
{
	int x, y;
	gtk_text_view_window_to_buffer_coords(w, GTK_TEXT_WINDOW_TEXT, ev->x, ev->y, &x, &y);
	gtk_text_view_get_iter_at_location(w, a, x, y);
	gtk_text_iter_set_line_offset(a, 0);
	return gtk_text_iter_get_line(a);
}

static GtkTextTag *tag(GtkTextBuffer *b, const char *name)
{
	return gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(b), name);
}

static void fold(GtkTextBuffer *b, int l, int hide)
{
	GtkTextIter s, e;
	gtk_text_buffer_get_iter_at_line(b, &s, l + 1);
	e = s;
	gtk_text_iter_forward_to_tag_toggle(&e, tag(b, "title"));
	if (hide < 0) hide = !gtk_text_iter_has_tag(&s, tag(b, "hide"));
	(hide ? gtk_text_buffer_apply_tag : gtk_text_buffer_remove_tag)(b, tag(b, "hide"), &s, &e);
	gtk_text_buffer_get_iter_at_line(b, &s, l);
	e = s;
	gtk_text_iter_forward_char(&e);
	gtk_text_buffer_delete(b, &s, &e);
	gtk_text_buffer_insert_with_tags(b, &s, hide ? "▸" : "▾", -1, tag(b, "title"), NULL);
}

static int stuck(GtkTextView *v, GtkTextIter *it, int *top, int *h)
{
	GtkTextTag *t = tag(gtk_text_view_get_buffer(v), "title");
	GdkRectangle r;
	GtkTextIter n;
	int y, d;
	gtk_text_view_get_visible_rect(v, &r);
	gtk_text_view_get_line_at_y(v, it, r.y, 0);
	if (!gtk_text_iter_has_tag(it, t)) {
		if (!gtk_text_iter_backward_to_tag_toggle(it, t)) return 0;
		if (!gtk_text_iter_has_tag(it, t)) gtk_text_iter_backward_char(it);
	}
	gtk_text_iter_set_line_offset(it, 0);
	gtk_text_view_get_line_yrange(v, it, &y, h);
	if (y >= r.y) return 0;
	*top = r.y;
	n = *it;
	if (gtk_text_iter_forward_line(&n) && (gtk_text_iter_has_tag(&n, t) || gtk_text_iter_forward_to_tag_toggle(&n, t))) {
		gtk_text_view_get_line_yrange(v, &n, &y, &d);
		if (y - r.y < *h) *top = y - *h;
	}
	return 1;
}

static int hit(GtkTextView *v, GdkEventButton *ev, GtkTextIter *it)
{
	int top, h, x, y;
	gtk_text_view_window_to_buffer_coords(v, GTK_TEXT_WINDOW_TEXT, ev->x, ev->y, &x, &y);
	return stuck(v, it, &top, &h) && y < top + h;
}

static gboolean stick(GtkTextView *v, cairo_t *cr)
{
	GtkStyleContext *c = gtk_widget_get_style_context(GTK_WIDGET(v));
	GtkTextIter it, e;
	GdkRectangle r;
	PangoLayout *l;
	char *s, *p, *q, *m;
	int top, h, x, y;
	if (!stuck(v, &it, &top, &h)) return FALSE;
	gtk_text_view_get_visible_rect(v, &r);
	gtk_text_view_buffer_to_window_coords(v, GTK_TEXT_WINDOW_WIDGET, r.x, top, &x, &y);
	gtk_render_background(c, cr, x, y, r.width, h);
	cairo_set_source_rgba(cr, .5, .5, .5, .2);
	cairo_rectangle(cr, x, y, r.width, h);
	cairo_fill(cr);
	e = it;
	gtk_text_iter_forward_to_line_end(&e);
	s = gtk_text_buffer_get_text(gtk_text_view_get_buffer(v), &it, &e, TRUE);
	if ((p = g_strrstr(s, "  +")) && (q = strstr(p, " −"))) {
		*p = *q = 0;
		m = g_markup_printf_escaped("<b>%s  <span foreground=\"#2a2\">%s</span> <span foreground=\"#d33\">%s</span></b>", s, p + 2, q + 1);
	} else m = g_markup_printf_escaped("<b>%s</b>", s);
	pango_layout_set_markup(l = gtk_widget_create_pango_layout(GTK_WIDGET(v), 0), m, -1);
	gtk_text_view_buffer_to_window_coords(v, GTK_TEXT_WINDOW_WIDGET, 0, top, &x, &y);
	gtk_render_layout(c, cr, x, y, l);
	g_object_unref(l);
	g_free(m);
	g_free(s);
	return FALSE;
}

static gboolean click(GtkTextView *w, GdkEventButton *ev, Hunk *hunk)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(w);
	GtkTextIter a, c;
	int l;
	if (ev->button == 1 && hit(w, ev, &a)) {
		fold(b, gtk_text_iter_get_line(&a), -1);
		return TRUE;
	}
	if (ev->button == 1 && !gtk_text_buffer_get_selection_bounds(b, &a, &c)) {
		l = line(w, ev, &a);
		if (gtk_text_iter_has_tag(&a, tag(b, "title"))) fold(b, l, -1);
		else if (hunk && gtk_text_iter_get_char(&a) == '@') hunk(map(GTK_WIDGET(w), l), -1);
	}
	return FALSE;
}

static gboolean press(GtkTextView *w, GdkEventButton *ev)
{
	GtkTextIter a;
	gunichar c;
	if (ev->button == 1 && hit(w, ev, &a)) return TRUE;
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
	if (GTK_IS_MENU(m)) menu(GTK_MENU(m), map(GTK_WIDGET(w), at), map(GTK_WIDGET(w), one));
}

static void all(GtkWidget *c, gpointer hide)
{
	diff_fold(g_object_get_data(G_OBJECT(c), "view"), GPOINTER_TO_INT(hide));
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
	g_object_set_data_full(G_OBJECT(v), "map", g_array_new(FALSE, FALSE, sizeof(int)), (GDestroyNotify)g_array_unref);
	g_signal_connect(v, "button-release-event", G_CALLBACK(click), hunk);
	g_signal_connect(v, "button-press-event", G_CALLBACK(press), 0);
	g_signal_connect_after(v, "draw", G_CALLBACK(stick), 0);
	if (menu) g_signal_connect(v, "populate-popup", G_CALLBACK(popup), menu);
	gtk_container_add(GTK_CONTAINER(s), v);
	gtk_widget_set_margin_start(s, 6);
	gtk_widget_set_margin_end(s, 6);
	gtk_widget_set_margin_bottom(s, 6);
	return s;
}

GtkWidget *diff_tools(GtkWidget *view)
{
	static const char *name[] = {"Expand all", "Collapse all"};
	GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *c;
	int k;
	for (k = 0; k < 2; k++) {
		gtk_box_pack_start(GTK_BOX(b), c = gtk_button_new_with_label(name[k]), FALSE, FALSE, 0);
		g_object_set_data(G_OBJECT(c), "view", view);
		g_signal_connect(c, "clicked", G_CALLBACK(all), GINT_TO_POINTER(k));
	}
	gtk_box_pack_start(GTK_BOX(b), c = gtk_label_new(0), TRUE, TRUE, 6);
	gtk_label_set_ellipsize(GTK_LABEL(c), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(c), 0);
	g_object_set_data(G_OBJECT(view), "sum", c);
	return b;
}

static void title(GtkTextBuffer *b, GArray *map, GtkTextMark *m, int at, const char *name, int len, int a, int d)
{
	GtkTextIter it;
	char *t = g_utf8_make_valid(name, len), s[32];
	gtk_text_buffer_get_iter_at_mark(b, &it, m);
	g_array_insert_val(map, gtk_text_iter_get_line(&it), at);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, "▾ ", -1, "title", NULL);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, t, -1, "title", NULL);
	snprintf(s, sizeof s, "  +%d", a);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, s, -1, "title", "+", NULL);
	snprintf(s, sizeof s, " −%d\n", d);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, s, -1, "title", "-", NULL);
	gtk_text_buffer_delete_mark(b, m);
	g_free(t);
}

void diff_files(GtkWidget *view, const char *text)
{
	GtkTextBuffer *b = buf(view);
	GArray *map = g_object_get_data(G_OBJECT(gtk_bin_get_child(GTK_BIN(view))), "map");
	GtkWidget *sum = g_object_get_data(G_OBJECT(view), "sum");
	GtkTextMark *m = 0;
	GtkTextIter it;
	const char *p, *e, *n, *name = 0;
	char *t, s[2] = "";
	int files = 0, hdr = 0, len = 0, a = 0, d = 0, add = 0, del = 0, top = 0, l, k;
	gtk_text_buffer_set_text(b, "", 0);
	gtk_text_buffer_get_start_iter(b, &it);
	g_array_set_size(map, 0);
	for (p = text, l = 0; p && *p; p = n, l++) {
		e = p + strcspn(p, "\n");
		n = e + !!*e;
		if (!strncmp(p, "diff --git ", 11)) {
			if (m) title(b, map, m, top, name, len, a, d);
			gtk_text_buffer_get_end_iter(b, &it);
			m = gtk_text_buffer_create_mark(b, 0, &it, TRUE);
			k = (e - p - 12) / 2;
			name = p + k + 14;
			len = e - name;
			top = l;
			hdr = 1;
			a = d = 0;
			files++;
			continue;
		}
		if (hdr && !strncmp(p, "+++ b/", 6)) name = p + 6, len = e - name;
		if (*p == '@') hdr = 0;
		if (hdr && (!strncmp(p, "index ", 6) || !strncmp(p, "--- ", 4) || !strncmp(p, "+++ ", 4))) continue;
		if (!hdr) a += *p == '+', d += *p == '-', add += *p == '+', del += *p == '-';
		*s = *p;
		t = g_utf8_make_valid(p, n - p);
		g_array_append_val(map, l);
		gtk_text_buffer_insert_with_tags_by_name(b, &it, t, -1, hdr ? "head" : strchr("@+-", *p) ? s : 0, NULL);
		g_free(t);
	}
	if (m) title(b, map, m, top, name, len, a, d);
	if (sum) {
		t = g_strdup_printf("%d file%s, +%d −%d", files, files == 1 ? "" : "s", add, del);
		gtk_label_set_text(GTK_LABEL(sum), t);
		g_free(t);
	}
}

void diff_fold(GtkWidget *view, int hide)
{
	GtkTextBuffer *b = buf(view);
	GtkTextIter it;
	int l, n = gtk_text_buffer_get_line_count(b);
	for (l = 0; l < n; l++) {
		gtk_text_buffer_get_iter_at_line(b, &it, l);
		if (gtk_text_iter_has_tag(&it, tag(b, "title"))) fold(b, l, hide);
	}
}

void diff_goto(GtkWidget *view, const char *path)
{
	GtkTextBuffer *b = buf(view);
	GtkTextIter it, e;
	char *t;
	int l, n = gtk_text_buffer_get_line_count(b), k = strlen(path), hit;
	for (l = 0; l < n; l++) {
		gtk_text_buffer_get_iter_at_line(b, &it, l);
		if (!gtk_text_iter_has_tag(&it, tag(b, "title"))) continue;
		e = it;
		gtk_text_iter_forward_to_line_end(&e);
		t = gtk_text_buffer_get_text(b, &it, &e, TRUE);
		hit = !strncmp(t + 4, path, k) && !strncmp(t + 4 + k, "  +", 3);
		g_free(t);
		if (!hit) continue;
		fold(b, l, 0);
		gtk_text_buffer_get_iter_at_line(b, &it, l);
		if (gtk_text_buffer_get_mark(b, "goto")) gtk_text_buffer_move_mark_by_name(b, "goto", &it);
		else gtk_text_buffer_create_mark(b, "goto", &it, TRUE);
		gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(gtk_bin_get_child(GTK_BIN(view))), gtk_text_buffer_get_mark(b, "goto"), 0, TRUE, 0, 0);
		return;
	}
}

int diff_lines(GtkWidget *view, int *from, int *to)
{
	GtkTextIter a, b;
	GtkWidget *v = gtk_bin_get_child(GTK_BIN(view));
	int r = gtk_text_buffer_get_selection_bounds(buf(view), &a, &b);
	*from = map(v, gtk_text_iter_get_line(&a));
	*to = map(v, gtk_text_iter_get_line(&b) - (gtk_text_iter_starts_line(&b) && !gtk_text_iter_equal(&a, &b)));
	return r;
}

