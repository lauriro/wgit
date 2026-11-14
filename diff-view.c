
#include <gtk/gtk.h>
#include <string.h>
#include "diff-view.h"
#include "ui.h"

typedef void Hunk(int from, int to);
typedef void Menu(GtkMenu *m, int hunk);
typedef void Top(const char *path);

/* A file list row has this much space above its arrow and text and below its text. */
#define PAD 3

/* A sixth of an em after the added count keeps it apart from the removed count. */
#define ADD "+%d\u2006"
#define DEL "-%d"

static int at = -1, one = -1;
static char *font;
static const char *tags[][2] = {
	{"head", "#57c"},
	{"@", "#888"},
	{"+", "#2a2"},
	{"-", "#d33"},
};

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

static void arrow(GtkTextView *v, GtkTextIter *it, int closed)
{
	GtkIconInfo *i = gtk_icon_theme_lookup_icon(gtk_icon_theme_get_default(), closed ? "pan-end-symbolic" : "pan-down-symbolic", 16, 0);
	GdkPixbuf *p = i ? gtk_icon_info_load_symbolic_for_context(i, gtk_widget_get_style_context(GTK_WIDGET(v)), 0, 0) : 0, *q;
	int m = gtk_text_view_get_left_margin(v);
	if (p) {
		q = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, m + gdk_pixbuf_get_width(p), gdk_pixbuf_get_height(p));
		gdk_pixbuf_fill(q, 0);
		gdk_pixbuf_copy_area(p, 0, 0, gdk_pixbuf_get_width(p), gdk_pixbuf_get_height(p) - PAD, q, m, PAD);
		gtk_text_buffer_insert_pixbuf(gtk_text_view_get_buffer(v), it, q);
		g_object_unref(q);
		g_object_unref(p);
	} else gtk_text_buffer_insert(gtk_text_view_get_buffer(v), it, closed ? "▸" : "▾", -1);
	if (i) g_object_unref(i);
}

static void fold(GtkTextView *v, int l, int hide)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(v);
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
	arrow(v, &s, hide);
	gtk_text_buffer_get_iter_at_line(b, &e, l);
	gtk_text_buffer_apply_tag(b, tag(b, "title"), &e, &s);
}

static int above(GtkTextView *v, GtkTextIter *it, int y)
{
	GtkTextTag *t = tag(gtk_text_view_get_buffer(v), "title");
	gtk_text_view_get_line_at_y(v, it, y, 0);
	if (!gtk_text_iter_has_tag(it, t)) {
		if (!gtk_text_iter_backward_to_tag_toggle(it, t)) return 0;
		if (!gtk_text_iter_has_tag(it, t)) gtk_text_iter_backward_char(it);
	}
	gtk_text_iter_set_line_offset(it, 0);
	return 1;
}

static void moved(GtkAdjustment *a, GtkTextView *v)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(v);
	GtkTextMark *g = gtk_text_buffer_get_mark(b, "goto");
	GtkTextIter it, e;
	GdkRectangle r;
	char *s, *p;
	int y, end, h;
	if (!g) return;
	gtk_text_view_get_visible_rect(v, &r);
	gtk_text_buffer_get_iter_at_mark(b, &it, g);
	gtk_text_view_get_line_yrange(v, &it, &y, &h);
	e = it;
	if (gtk_text_iter_forward_line(&e) && !gtk_text_iter_has_tag(&e, tag(b, "title"))) gtk_text_iter_forward_to_tag_toggle(&e, tag(b, "title"));
	gtk_text_view_get_line_yrange(v, &e, &end, &h);
	if (!gtk_text_iter_has_tag(&e, tag(b, "title"))) end += h;
	if ((y < r.y + r.height && end > r.y) || !above(v, &it, r.y)) return;
	gtk_text_buffer_move_mark(b, g, &it);
	e = it;
	gtk_text_iter_forward_to_line_end(&e);
	s = gtk_text_buffer_get_text(b, &it, &e, TRUE);
	if ((p = g_strrstr(s, "  "))) {
		*p = 0;
		((Top *)g_object_get_data(G_OBJECT(v), "top"))(s + 1);
	}
	g_free(s);
}

static int stuck(GtkTextView *v, GtkTextIter *it, int *top, int *h)
{
	GtkTextTag *t = tag(gtk_text_view_get_buffer(v), "title");
	GdkRectangle r;
	GtkTextIter n;
	int y, d;
	gtk_text_view_get_visible_rect(v, &r);
	if (!above(v, it, r.y)) return 0;
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
	GdkPixbuf *pb;
	PangoLayout *l;
	char *s, *p, *q, *m;
	int top, h, x, y, lh, w = 0;
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
	*(p = g_strrstr(s, "  ")) = 0;
	if ((q = strchr(p += 2, '-'))) *q++ = 0;
	m = g_markup_printf_escaped("<span font=\"%s\"><b>%s</b>  <span foreground=\"#2a2\">%s</span><span foreground=\"#d33\">%s%s</span></span>", font, s, p, q ? "-" : "", q ? q : "");
	pango_layout_set_markup(l = gtk_widget_create_pango_layout(GTK_WIDGET(v), 0), m, -1);
	pango_layout_get_pixel_size(l, 0, &lh);
	y += h - PAD - lh;
	if ((pb = gtk_text_iter_get_pixbuf(&it))) {
		gdk_cairo_set_source_pixbuf(cr, pb, x, y + pango_layout_get_baseline(l) / PANGO_SCALE - gdk_pixbuf_get_height(pb));
		cairo_paint(cr);
		w = gdk_pixbuf_get_width(pb);
	}
	gtk_render_layout(c, cr, x + w, y, l);
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
		fold(w, gtk_text_iter_get_line(&a), -1);
		return TRUE;
	}
	if (ev->button == 1 && !gtk_text_buffer_get_selection_bounds(b, &a, &c)) {
		l = line(w, ev, &a);
		if (gtk_text_iter_has_tag(&a, tag(b, "title"))) fold(w, l, -1);
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
	if (GTK_IS_MENU(m)) menu(GTK_MENU(m), map(GTK_WIDGET(w), at));
}

static void all(GtkWidget *c, gpointer hide)
{
	diff_fold(g_object_get_data(G_OBJECT(c), "view"), GPOINTER_TO_INT(hide));
}

GtkWidget *diff_view(Hunk *hunk, Menu *menu, Top *top)
{
	GtkWidget *v = g_object_new(GTK_TYPE_TEXT_VIEW, "editable", FALSE, "monospace", TRUE, "left-margin", 6, "right-margin", 6, "bottom-margin", 6, NULL);
	GtkWidget *s = scroll(v);
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	int i;
	for (i = 0; i < 4; i++) gtk_text_buffer_create_tag(b, tags[i][0], "foreground", tags[i][1], NULL);
	if (!font) g_object_get(gtk_settings_get_default(), "gtk-font-name", &font, NULL);
	gtk_text_buffer_create_tag(b, "title", "font", font, "pixels-below-lines", PAD, "left-margin", 0, "right-margin", 0, "paragraph-background", "rgba(128,128,128,0.2)", NULL);
	gtk_text_buffer_create_tag(b, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
	gtk_text_buffer_create_tag(b, "hide", "invisible", TRUE, NULL);
	g_object_set_data(G_OBJECT(v), "map", g_array_new(FALSE, FALSE, sizeof(int)));
	g_signal_connect(v, "button-release-event", G_CALLBACK(click), hunk);
	g_signal_connect(v, "button-press-event", G_CALLBACK(press), 0);
	g_signal_connect_after(v, "draw", G_CALLBACK(stick), 0);
	if (menu) g_signal_connect(v, "populate-popup", G_CALLBACK(popup), menu);
	if (top) {
		g_object_set_data(G_OBJECT(v), "top", top);
		g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(s)), "value-changed", G_CALLBACK(moved), v);
	}
	return v;
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
	gtk_box_pack_start(GTK_BOX(b), c = g_object_new(GTK_TYPE_LABEL, "ellipsize", PANGO_ELLIPSIZE_END, "xalign", 0., "use-markup", TRUE, NULL), TRUE, TRUE, 6);
	g_object_set_data(G_OBJECT(view), "sum", c);
	return b;
}

static const char *num(char *s, const char *fmt, int n)
{
	*s = 0;
	if (n) snprintf(s, 16, fmt, n);
	return s;
}

static void title(GtkTextView *v, GArray *map, GtkTextMark *m, int at, const char *name, int len, int a, int d)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(v);
	GtkTextIter it, st;
	char s[16];
	gtk_text_buffer_get_iter_at_mark(b, &it, m);
	g_array_insert_val(map, gtk_text_iter_get_line(&it), at);
	arrow(v, &it, 0);
	gtk_text_buffer_insert(b, &it, " ", -1);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, name, len, "bold", NULL);
	gtk_text_buffer_insert(b, &it, "  ", -1);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, num(s, ADD, a), -1, "+", NULL);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, num(s, DEL, d), -1, "-", NULL);
	gtk_text_buffer_insert(b, &it, "\n", -1);
	gtk_text_buffer_get_iter_at_mark(b, &st, m);
	gtk_text_buffer_apply_tag(b, tag(b, "title"), &st, &it);
	gtk_text_buffer_delete_mark(b, m);
}

void diff_files(GtkWidget *view, const char *text)
{
	GtkTextView *tv = GTK_TEXT_VIEW(view);
	GtkTextBuffer *b = gtk_text_view_get_buffer(tv);
	GArray *map = g_object_get_data(G_OBJECT(view), "map");
	GtkWidget *sum = g_object_get_data(G_OBJECT(view), "sum");
	GtkTextMark *m = 0;
	GtkTextIter it;
	const char *p, *e, *n, *name = 0;
	char *t = g_utf8_make_valid(text ? text : "", -1), s[2] = "", x[16], y[16];
	int files = 0, hdr = 0, len = 0, a = 0, d = 0, add = 0, del = 0, top = 0, l, k;
	gtk_text_buffer_set_text(b, "", 0);
	gtk_text_buffer_get_end_iter(b, &it);
	g_array_set_size(map, 0);
	for (p = t, l = 0; *p; p = n, l++) {
		e = p + strcspn(p, "\n");
		n = e + !!*e;
		if (!strncmp(p, "diff --git ", 11)) {
			if (m) title(tv, map, m, top, name, len, a, d);
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
		g_array_append_val(map, l);
		gtk_text_buffer_insert_with_tags_by_name(b, &it, p, n - p, hdr ? "head" : strchr("@+-", *p) ? s : 0, NULL);
	}
	if (m) title(tv, map, m, top, name, len, a, d);
	g_free(t);
	if (sum) {
		t = g_strdup_printf("%d file%s  <span foreground=\"%s\">%s</span><span foreground=\"%s\">%s</span>", files, files == 1 ? "" : "s", tags[2][1], num(x, ADD, add), tags[3][1], num(y, DEL, del));
		g_object_set(sum, "label", t, NULL);
		g_free(t);
	}
}

void diff_fold(GtkWidget *view, int hide)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter it;
	int l, n = gtk_text_buffer_get_line_count(b);
	for (l = 0; l < n; l++) {
		gtk_text_buffer_get_iter_at_line(b, &it, l);
		if (gtk_text_iter_has_tag(&it, tag(b, "title"))) fold(GTK_TEXT_VIEW(view), l, hide);
	}
}

void diff_goto(GtkWidget *view, const char *path)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextMark *g;
	GtkTextIter it, e;
	char *t, *p;
	int l, n = gtk_text_buffer_get_line_count(b), k = strlen(path), hit;
	for (l = 0; l < n; l++) {
		gtk_text_buffer_get_iter_at_line(b, &it, l);
		if (!gtk_text_iter_has_tag(&it, tag(b, "title"))) continue;
		e = it;
		gtk_text_iter_forward_to_line_end(&e);
		t = gtk_text_buffer_get_text(b, &it, &e, TRUE);
		hit = (p = strchr(t, ' ')) && !strncmp(p + 1, path, k) && g_strrstr(t, "  ") == p + 1 + k;
		g_free(t);
		if (!hit) continue;
		fold(GTK_TEXT_VIEW(view), l, 0);
		gtk_text_buffer_get_iter_at_line(b, &it, l);
		if ((g = gtk_text_buffer_get_mark(b, "goto"))) gtk_text_buffer_move_mark(b, g, &it);
		else g = gtk_text_buffer_create_mark(b, "goto", &it, TRUE);
		gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(view), g, 0, TRUE, 0, 0);
		return;
	}
}

int diff_lines(GtkWidget *view, int *from, int *to)
{
	GtkTextIter a, b;
	if (!gtk_text_buffer_get_selection_bounds(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), &a, &b)) return *from = *to = map(view, one), one >= 0;
	*from = map(view, gtk_text_iter_get_line(&a));
	*to = map(view, gtk_text_iter_get_line(&b) - gtk_text_iter_starts_line(&b));
	return 1;
}

