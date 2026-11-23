
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "diff-view.h"
#include "ui.h"

typedef void Hunk(int from, int to);
typedef void Menu(GMenu *m, int hunk);
typedef void Top(const char *path);

/* A file list row has this much space above its arrow and text and below its text.
 * The arrow image is as tall as the text above its base line plus this space, so a title is as tall as a list row
 * with any font; asc is that height of the text. */
#define PAD 3
#define MARGIN 6

/* A sixth of an em after the added count keeps it apart from the removed count. */
#define ADD "+%ld\u2006"
#define DEL "-%ld"

/* GTK scrolls to a file in steps. From diff_goto() until the pointer is used on the diff, a scroll is not the user's,
 * so it must not move the selection in the file list. */
static int at = -1, one = -1, jump, hold, asc;
static char *font;
static const char *tags[][2] = {
	{"head", "#57c"},
	{"@", "#888"},
	{"+", "#2a2"},
	{"-", "#d33"},
};

static int map(GtkWidget *w, int l)
{
	int *m = g_object_get_data(G_OBJECT(w), "map");
	return !m || l < 0 || l >= *m ? l : m[l + 1];
}

static int line(GtkTextView *w, double ex, double ey, GtkTextIter *a)
{
	int x, y;
	gtk_text_view_window_to_buffer_coords(w, GTK_TEXT_WINDOW_WIDGET, ex, ey, &x, &y);
	gtk_text_view_get_iter_at_location(w, a, x, y);
	gtk_text_iter_set_line_offset(a, 0);
	return gtk_text_iter_get_line(a);
}

static GtkTextTag *tag(GtkTextBuffer *b, const char *name)
{
	return g_object_get_data(G_OBJECT(b), name);
}

/* The icon is drawn once in the text's colour, as GTK 3 loaded a symbolic icon for the style.
 * GTK 4 has the pan icons built in, so there is always one */
static void arrow(GtkTextView *v, GtkTextIter *it, int closed)
{
	GtkWidget *w = GTK_WIDGET(v);
	GtkIconPaintable *i = gtk_icon_theme_lookup_icon(gtk_icon_theme_get_for_display(gtk_widget_get_display(w)), closed ? "pan-end-symbolic" : "pan-down-symbolic", 0, 16, 1, GTK_TEXT_DIR_NONE, 0);
	GtkSnapshot *s = gtk_snapshot_new();
	GdkPaintable *p;
	GdkRGBA c;
	gtk_widget_get_color(w, &c);
	gtk_snapshot_push_clip(s, &GRAPHENE_RECT_INIT(0, 0, MARGIN + 16, asc + PAD));
	gtk_snapshot_translate(s, &GRAPHENE_POINT_INIT(MARGIN, MAX(asc + 2 * PAD - 16, 0)));
	gtk_symbolic_paintable_snapshot_symbolic(GTK_SYMBOLIC_PAINTABLE(i), s, 16, 16, &c, 1);
	gtk_snapshot_pop(s);
	gtk_text_buffer_insert_paintable(gtk_text_view_get_buffer(v), it, p = gtk_snapshot_free_to_paintable(s, &GRAPHENE_SIZE_INIT(MARGIN + 16, asc + PAD)));
	g_object_unref(p);
	g_object_unref(i);
}

/* GTK's diff of two frames missed parts of the lines a fold took away, on any renderer: they stayed on the screen until
 * the compositor drew the window again. A widget's opacity changed is no diff but the whole widget: for a second after
 * a fold, the window's child takes another at each frame, one no eye sees, and each frame repaints the window whole */
static gint64 whole;
static int flashing;

static gboolean flash(GtkWidget *w, GdkFrameClock *c, gpointer d)
{
	flashing = gdk_frame_clock_get_frame_time(c) < whole;
	gtk_widget_set_opacity(w, flashing ? (gtk_widget_get_opacity(w) < .995 ? 254 : 253) / 255. : 1);
	return flashing;
}

static void fold(GtkTextView *v, int l, int hide)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(v);
	GtkRoot *r = gtk_widget_get_root(GTK_WIDGET(v));
	GtkTextIter s, e;
	whole = g_get_monotonic_time() + G_USEC_PER_SEC;
	if (!flashing && GTK_IS_WINDOW(r)) flashing = 1, gtk_widget_add_tick_callback(gtk_window_get_child(GTK_WINDOW(r)), flash, 0, 0);
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

/* A name with a U+2029 splits its title into two lines, so a title can lack the space or the counts. */
static char *named(GtkTextBuffer *b, GtkTextIter *it, char **tail)
{
	GtkTextIter e = *it;
	char *s, *p;
	gtk_text_iter_forward_to_line_end(&e);
	s = gtk_text_buffer_get_text(b, it, &e, TRUE);
	if ((p = g_strrstr(s, "  "))) *p = 0, p += 2;
	*tail = p ? p : s + strlen(s);
	return s;
}

static void moved(GtkAdjustment *a, GtkTextView *v)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(v);
	GtkTextMark *g = gtk_text_buffer_get_mark(b, "goto");
	GtkTextIter it, e;
	GdkRectangle r;
	char *s, *p;
	int y, end, h;
	if (!g || jump || hold) return;
	gtk_text_view_get_visible_rect(v, &r);
	gtk_text_buffer_get_iter_at_mark(b, &it, g);
	gtk_text_view_get_line_yrange(v, &it, &y, &h);
	e = it;
	if (gtk_text_iter_forward_line(&e) && !gtk_text_iter_has_tag(&e, tag(b, "title"))) gtk_text_iter_forward_to_tag_toggle(&e, tag(b, "title"));
	gtk_text_view_get_line_yrange(v, &e, &end, &h);
	if (!gtk_text_iter_has_tag(&e, tag(b, "title"))) end += h;
	if ((y < r.y + r.height && end > r.y) || !above(v, &it, r.y)) return;
	gtk_text_buffer_move_mark(b, g, &it);
	s = named(b, &it, &p);
	if ((p = strchr(s, ' '))) ((Top *)g_object_get_data(G_OBJECT(v), "top"))(p + 1);
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

static int hit(GtkTextView *v, double ex, double ey, GtkTextIter *it)
{
	int top, h, x, y;
	gtk_text_view_window_to_buffer_coords(v, GTK_TEXT_WINDOW_WIDGET, ex, ey, &x, &y);
	return stuck(v, it, &top, &h) && y < top + h;
}

/* The text view's own background under the title: GTK 4 gives a widget's colour but not its background */
static void stick(GtkTextView *v, GtkSnapshot *sn)
{
	GtkWidget *wv = GTK_WIDGET(v);
	GtkTextIter it;
	GdkRectangle r;
	GdkPaintable *pb;
	PangoLayout *l;
	GdkRGBA c;
	char *s, *p, *q, *m;
	int top, h, x, y, lh, d, w = 0;
	if (!stuck(v, &it, &top, &h)) return;
	gtk_text_view_get_visible_rect(v, &r);
	gtk_text_view_buffer_to_window_coords(v, GTK_TEXT_WINDOW_WIDGET, r.x, top, &x, &y);
	gtk_snapshot_render_background(sn, gtk_widget_get_style_context(wv), x, y, r.width, h);
	gtk_snapshot_append_color(sn, &(GdkRGBA){.5, .5, .5, .2}, &GRAPHENE_RECT_INIT(x, y, r.width, h));
	s = named(gtk_text_view_get_buffer(v), &it, &p);
	if ((q = strchr(p, '-'))) *q++ = 0;
	m = g_markup_printf_escaped("<span font=\"%s\"><b>%s</b>  <span foreground=\"#2a2\">%s</span><span foreground=\"#d33\">%s%s</span></span>", font, s, p, q ? "-" : "", q ? q : "");
	pango_layout_set_markup(l = gtk_widget_create_pango_layout(wv, 0), m, -1);
	pango_layout_get_pixel_size(l, 0, &lh);
	gtk_snapshot_save(sn);
	gtk_snapshot_translate(sn, &GRAPHENE_POINT_INIT(x, y + h - PAD - lh));
	if ((pb = gtk_text_iter_get_paintable(&it))) {
		w = gdk_paintable_get_intrinsic_width(pb);
		d = pango_layout_get_baseline(l) / PANGO_SCALE - gdk_paintable_get_intrinsic_height(pb);
		gtk_snapshot_translate(sn, &GRAPHENE_POINT_INIT(0, d));
		gdk_paintable_snapshot(pb, sn, w, gdk_paintable_get_intrinsic_height(pb));
		gtk_snapshot_translate(sn, &GRAPHENE_POINT_INIT(w, -d));
	}
	gtk_widget_get_color(wv, &c);
	gtk_snapshot_append_layout(sn, l, &c);
	gtk_snapshot_restore(sn);
	g_object_unref(l);
	g_free(m);
	g_free(s);
}

/* The title of the file at the top is drawn over the text, as the GTK 3 draw signal did after the view */
typedef GtkTextView Diff;
typedef GtkTextViewClass DiffClass;
G_DEFINE_TYPE(Diff, diff, GTK_TYPE_TEXT_VIEW)

static void diff_snapshot(GtkWidget *w, GtkSnapshot *s)
{
	GTK_WIDGET_CLASS(diff_parent_class)->snapshot(w, s);
	stick(GTK_TEXT_VIEW(w), s);
}

static void diff_class_init(DiffClass *c)
{
	GTK_WIDGET_CLASS(c)->snapshot = diff_snapshot;
}

static void diff_init(Diff *d)
{
}

static gboolean click(GtkTextView *w, double x, double y)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(w);
	Hunk *hunk = g_object_get_data(G_OBJECT(w), "hunk");
	GtkTextIter a, c;
	int l;
	/* While lines are edited, a click only moves the cursor */
	if (gtk_text_view_get_editable(w)) return FALSE;
	if (hit(w, x, y, &a)) {
		fold(w, gtk_text_iter_get_line(&a), -1);
		return TRUE;
	}
	if (!gtk_text_buffer_get_selection_bounds(b, &a, &c)) {
		l = line(w, x, y, &a);
		if (gtk_text_iter_has_tag(&a, tag(b, "title"))) fold(w, l, -1);
		else if (hunk && gtk_text_iter_get_char(&a) == '@') hunk(map(GTK_WIDGET(w), l), -1);
	}
	return FALSE;
}

/* The menu is the view's extra menu, filled again before each popup. The line under the pointer is for one menu only:
 * a menu from the keyboard has no line. */
static void popup(GtkTextView *w)
{
	GMenu *m = G_MENU(gtk_text_view_get_extra_menu(w));
	g_menu_remove_all(m);
	((Menu *)g_object_get_data(G_OBJECT(w), "menu"))(m, map(GTK_WIDGET(w), at));
	at = one = -1;
}

/* Only a view with a menu keeps the line: the views share at and one. */
static gboolean press(GtkTextView *w, int n, double x, double y)
{
	GtkTextIter a;
	gunichar c;
	if (n == 1 && hit(w, x, y, &a)) return TRUE;
	if (n == 3 && g_object_get_data(G_OBJECT(w), "menu")) {
		one = at = line(w, x, y, &a);
		c = gtk_text_iter_get_char(&a);
		for (; at >= 0 && gtk_text_iter_get_char(&a) != '@'; gtk_text_iter_backward_line(&a))
			at = gtk_text_iter_has_tag(&a, tag(gtk_text_view_get_buffer(w), "title")) ? -1 : at - 1;
		if (at < 0 || (c != '+' && c != '-')) one = -1;
		popup(w);
	}
	return FALSE;
}

/* Pointer use: a motion or a scroll ends a scroll to a file, a scroll or a press ends a place kept over a reload,
 * a pointer only passing does not */
static gboolean event(GtkEventController *ec, GdkEvent *ev)
{
	GtkTextView *w = GTK_TEXT_VIEW(gtk_event_controller_get_widget(ec));
	GdkEventType t = gdk_event_get_event_type(ev);
	guint k;
	double x, y;
	int n = 0;
	if (t == GDK_BUTTON_PRESS || t == GDK_BUTTON_RELEASE) n = gdk_button_event_get_button(ev), local(GTK_WIDGET(w), ev, &x, &y);
	if (t == GDK_BUTTON_RELEASE) return n == 1 && click(w, x, y);
	if (t == GDK_BUTTON_PRESS && press(w, n, x, y)) return TRUE;
	if (t == GDK_KEY_PRESS && g_object_get_data(G_OBJECT(w), "menu")) {
		k = gdk_key_event_get_keyval(ev);
		if (k == GDK_KEY_Menu || (k == GDK_KEY_F10 && gdk_event_get_modifier_state(ev) & GDK_SHIFT_MASK)) popup(w);
	}
	if (g_object_get_data(G_OBJECT(w), "top")) {
		if (t == GDK_MOTION_NOTIFY || t == GDK_SCROLL) jump = 0;
		if (t == GDK_SCROLL || t == GDK_BUTTON_PRESS) hold = 0;
	}
	return FALSE;
}

/* A place kept over a reload, as a scroll value: set at once, never scrolled to, which GTK animates. GTK measures a
 * new text bit by bit and holds the value within what it has measured, so it is set again as the text grows, and last
 * in an idle below GTK's own measuring, when the whole text is measured. The text above an edit is as it was */
static double kept;

static gboolean settle(GtkTextView *v)
{
	if (hold) gtk_adjustment_set_value(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(v)), kept), hold = 0;
	return G_SOURCE_REMOVE;
}

static void aim(GtkTextView *v)
{
	GtkTextMark *g = gtk_text_buffer_get_mark(gtk_text_view_get_buffer(v), "goto");
	if (g && jump) gtk_text_view_scroll_to_mark(v, g, 0, TRUE, 0, 0);
}

/* GTK measures a new text bit by bit, and a scroll cannot pass the height measured so far,
 * so the scroll to a file is done again each time the height changes. */
static void grown(GtkAdjustment *a, GtkTextView *v)
{
	if (hold) gtk_adjustment_set_value(a, kept);
	else aim(v);
}

static void all(GtkWidget *c, gpointer hide)
{
	diff_fold(g_object_get_data(G_OBJECT(c), "view"), "", 0, GPOINTER_TO_INT(hide));
}

static void recolor(GtkTextBuffer *b);

GtkWidget *diff_view(Hunk *hunk, Menu *menu, Top *top)
{
	GtkWidget *v = g_object_new(diff_get_type(), "editable", FALSE, "cursor-visible", FALSE, "monospace", TRUE, "left-margin", MARGIN, "right-margin", MARGIN, "bottom-margin", MARGIN, NULL);
	GtkWidget *s = scroll(v);
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
	PangoLayout *l;
	GMenu *m;
	char *t;
	int i;
	for (i = 0; i < 4; i++) gtk_text_buffer_create_tag(b, tags[i][0], "foreground", tags[i][1], NULL);
	if (!font) {
		g_object_get(gtk_settings_get_default(), "gtk-font-name", &font, NULL);
		t = g_markup_printf_escaped("<span font=\"%s\" weight=\"bold\">x</span>", font);
		pango_layout_set_markup(l = gtk_widget_create_pango_layout(v, 0), t, -1);
		asc = pango_layout_get_baseline(l) / PANGO_SCALE;
		g_object_unref(l);
		g_free(t);
	}
	g_object_set_data(G_OBJECT(b), "title", gtk_text_buffer_create_tag(b, "title", "font", font, "pixels-below-lines", PAD, "left-margin", 0, "right-margin", 0, "paragraph-background", "rgba(128,128,128,0.2)", NULL));
	gtk_text_buffer_create_tag(b, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
	/* An edit: the yellow block of lines that alone takes typing */
	gtk_text_buffer_create_tag(b, "lock", "editable", FALSE, NULL);
	/* The block keeps its text where it was: a margin of one character where the + or space was */
	pango_layout_get_pixel_size(l = gtk_widget_create_pango_layout(v, "x"), &i, 0);
	g_object_unref(l);
	gtk_text_buffer_create_tag(b, "edit", "paragraph-background", "rgba(255,210,0,0.25)", "left-margin", MARGIN + i, NULL);
	g_signal_connect(b, "changed", G_CALLBACK(recolor), 0);
	g_object_set_data(G_OBJECT(b), "hide", gtk_text_buffer_create_tag(b, "hide", "invisible", TRUE, NULL));
	g_object_set_data(G_OBJECT(v), "hunk", hunk);
	g_object_set_data(G_OBJECT(v), "menu", menu);
	g_object_set_data(G_OBJECT(v), "top", top);
	events(v, G_CALLBACK(event), 0);
	if (menu) {
		gtk_text_view_set_extra_menu(GTK_TEXT_VIEW(v), G_MENU_MODEL(m = g_menu_new()));
		g_object_unref(m);
	}
	if (top) {
		g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(s)), "changed", G_CALLBACK(grown), v);
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
		gtk_box_append(GTK_BOX(b), c = gtk_button_new_with_label(name[k]));
		g_object_set_data(G_OBJECT(c), "view", view);
		g_signal_connect(c, "clicked", G_CALLBACK(all), GINT_TO_POINTER(k));
	}
	gtk_box_append(GTK_BOX(b), c = g_object_new(GTK_TYPE_LABEL, "ellipsize", PANGO_ELLIPSIZE_END, "xalign", 0., "use-markup", TRUE, "hexpand", TRUE, "margin-start", 6, "margin-end", 6, NULL));
	g_object_set_data(G_OBJECT(view), "sum", c);
	return b;
}

static const char *num(char *s, const char *fmt, long n)
{
	*s = 0;
	if (n) snprintf(s, 16, fmt, n);
	return s;
}

void diff_counts(GtkTextBuffer *b, GtkTextIter *it, long a, long d)
{
	char s[32], *face;
	int i;
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table(b);
	if (!gtk_text_tag_table_lookup(table, "counts")) {
		g_object_get(gtk_settings_get_default(), "gtk-font-name", &face, NULL);
		gtk_text_buffer_create_tag(b, "counts", "font", face, NULL);
		g_free(face);
		for (i = 2; i < 4; i++) if (!gtk_text_tag_table_lookup(table, tags[i][0]))
			gtk_text_buffer_create_tag(b, tags[i][0], "foreground", tags[i][1], NULL);
	}
	if (a) {
		snprintf(s, sizeof s, ADD, a);
		gtk_text_buffer_insert_with_tags_by_name(b, it, s, -1, "+", "counts", NULL);
	}
	if (d) {
		snprintf(s, sizeof s, DEL, d);
		gtk_text_buffer_insert_with_tags_by_name(b, it, s, -1, "-", "counts", NULL);
	}
}

static void title(GtkTextView *v, GtkTextMark *m, const char *name, int len, int a, int d)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(v);
	GtkTextIter it, st;
	gtk_text_buffer_get_iter_at_mark(b, &it, m);
	arrow(v, &it, 0);
	gtk_text_buffer_insert(b, &it, " ", -1);
	gtk_text_buffer_insert_with_tags_by_name(b, &it, name, len, "bold", NULL);
	gtk_text_buffer_insert(b, &it, "  ", -1);
	diff_counts(b, &it, a, d);
	gtk_text_buffer_insert(b, &it, "\n", -1);
	gtk_text_buffer_get_iter_at_mark(b, &st, m);
	gtk_text_buffer_apply_tag(b, tag(b, "title"), &st, &it);
	gtk_text_buffer_delete_mark(b, m);
}

/* The title of a file is put in last, when its counts are known, so it gets its place in the map at the first line
 * of the file. The map starts with its length and doubles its room. */
void diff_files(GtkWidget *view, const char *text)
{
	GtkTextView *tv = GTK_TEXT_VIEW(view);
	GtkTextBuffer *b = gtk_text_view_get_buffer(tv);
	GtkWidget *sum = g_object_get_data(G_OBJECT(view), "sum");
	GtkTextMark *m = 0;
	GtkTextIter it;
	const char *p, *e, *n, *name = 0;
	char *t = g_utf8_make_valid(text ? text : "", -1), s[2] = "", x[16], y[16];
	int files = 0, hdr = 0, len = 0, a = 0, d = 0, add = 0, del = 0, l, k = 0;
	int *map = g_object_get_data(G_OBJECT(view), "map");
	/* A new diff ends an edit: what was typed and not saved goes */
	if (gtk_text_buffer_get_mark(b, "edit")) gtk_text_buffer_delete_mark_by_name(b, "edit"), gtk_text_buffer_delete_mark_by_name(b, "edit-end");
	g_object_set(view, "editable", FALSE, "cursor-visible", FALSE, NULL);
	hold = 0;
	gtk_text_buffer_set_text(b, "", 0);
	gtk_text_buffer_get_end_iter(b, &it);
	for (p = t, l = 0;; p = n, l++) {
		if (!(k & (k + 1))) map = realloc(map, (2 * k + 3) * sizeof *map);
		if (!*p) break;
		e = p + strcspn(p, "\n");
		n = e + !!*e;
		if (hdr && (!strncmp(p, "index ", 6) || !strncmp(p, "--- ", 4) || !strncmp(p, "+++ ", 4))) continue;
		if (hdr && (!strncmp(p, "rename to ", 10) || !strncmp(p, "copy to ", 8))) name = p + (*p == 'r' ? 10 : 8), len = e - name;
		map[++k] = l;
		/* An unmerged file has `diff --cc name`, or `* Unmerged path name` in the index.
		 * Else the two names are equal and the second half of the line is the name, or `rename to` or `copy to` has it. */
		if (!strncmp(p, "diff --", 7) || *p == '*') {
			if (m) title(tv, m, name, len, a, d);
			gtk_text_buffer_get_end_iter(b, &it);
			m = gtk_text_buffer_create_mark(b, 0, &it, TRUE);
			name = p + (*p == '*' ? 16 : p[7] == 'c' ? 10 : (e - p + 16) / 2);
			len = e - name;
			hdr = 1;
			a = d = 0;
			files++;
			continue;
		}
		if (*p == '@') hdr = 0;
		if (!hdr) a += *p == '+', d += *p == '-', add += *p == '+', del += *p == '-';
		*s = *p;
		gtk_text_buffer_insert_with_tags_by_name(b, &it, p, n - p, hdr ? "head" : strchr("@+-", *p) ? s : 0, NULL);
	}
	if (m) title(tv, m, name, len, a, d);
	g_free(t);
	*map = k;
	g_object_set_data(G_OBJECT(view), "map", map);
	if (sum) {
		t = g_strdup_printf("%d file%s  <span foreground=\"%s\">%s</span><span foreground=\"%s\">%s</span>", files, files == 1 ? "" : "s", tags[2][1], num(x, ADD, add), tags[3][1], num(y, DEL, del));
		g_object_set(sum, "label", t, NULL);
		g_free(t);
	}
}

/* A path can have two titles, as a file that became a link has; diff_goto() needs the first */
int diff_fold(GtkWidget *view, const char *dir, int len, int hide)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter it;
	char *t, *p;
	int l, r = -1, n = gtk_text_buffer_get_line_count(b);
	for (l = 0; l < n; l++) {
		gtk_text_buffer_get_iter_at_line(b, &it, l);
		if (!gtk_text_iter_has_tag(&it, tag(b, "title"))) continue;
		t = named(b, &it, &p);
		if ((p = strchr(t, ' ')) && !strncmp(p + 1, dir, len)) {
			fold(GTK_TEXT_VIEW(view), l, hide);
			if (r < 0) r = l;
		}
		g_free(t);
	}
	return r;
}

/* A whole name is a prefix that has the end of the name in it */
/* A scroll to a line that holds while GTK measures the text, until the view is used */
static void scroll_to(GtkWidget *view, int l)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextMark *g;
	GtkTextIter it;
	gtk_text_buffer_get_iter_at_line(b, &it, l);
	if ((g = gtk_text_buffer_get_mark(b, "goto"))) gtk_text_buffer_move_mark(b, g, &it);
	else gtk_text_buffer_create_mark(b, "goto", &it, TRUE);
	jump = 1;
	aim(GTK_TEXT_VIEW(view));
}

/* The scroll value before a reload, and back to it after */
double diff_top(GtkWidget *view)
{
	return gtk_adjustment_get_value(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view)));
}

void diff_keep(GtkWidget *view, double value)
{
	kept = value, hold = 1;
	gtk_adjustment_set_value(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view)), kept);
	g_idle_add_full(G_PRIORITY_LOW, (GSourceFunc)settle, view, 0);
}

void diff_goto(GtkWidget *view, const char *path)
{
	int l = diff_fold(view, path, strlen(path) + 1, 0);
	if (l >= 0) scroll_to(view, l);
}

int diff_lines(GtkWidget *view, int *from, int *to)
{
	GtkTextIter a, b;
	if (!gtk_text_buffer_get_selection_bounds(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), &a, &b)) return *from = *to = map(view, one), one >= 0;
	*from = map(view, gtk_text_iter_get_line(&a));
	*to = map(view, gtk_text_iter_get_line(&b) - gtk_text_iter_starts_line(&b));
	return 1;
}


/* The yellow follows the block as it is typed: typed text takes no tags */
static void recolor(GtkTextBuffer *b)
{
	GtkTextMark *m = gtk_text_buffer_get_mark(b, "edit"), *n = gtk_text_buffer_get_mark(b, "edit-end");
	GtkTextIter a, z;
	if (!m || !n) return;
	gtk_text_buffer_get_iter_at_mark(b, &a, m), gtk_text_buffer_get_iter_at_mark(b, &z, n);
	gtk_text_iter_forward_char(&z);
	gtk_text_buffer_apply_tag_by_name(b, "edit", &a, &z);
}

/* Lines from to of the diff as their file has them, without their + and space, in a yellow block that alone takes
 * typing. Its last newline stays locked: GTK lets a boundary take text when the character before it is editable */
void diff_edit(GtkWidget *view, int from, int to)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter a, z, x;
	GString *s = g_string_new(0);
	int *m = g_object_get_data(G_OBJECT(view), "map"), i, first = -1, last = -1;
	char *t;
	for (i = 0; m && i < *m; i++) {
		if (m[i + 1] == from && first < 0) first = i;
		if (m[i + 1] == to) last = i;
	}
	if (first < 0 || last < first) {
		g_string_free(s, TRUE);
		return;
	}
	for (i = first; i <= last; i++) {
		gtk_text_buffer_get_iter_at_line(b, &a, i);
		z = a;
		if (!gtk_text_iter_ends_line(&z)) gtk_text_iter_forward_to_line_end(&z);
		t = gtk_text_iter_get_text(&a, &z);
		g_string_append(s, *t ? t + 1 : t), g_string_append_c(s, '\n');
		g_free(t);
	}
	gtk_text_buffer_get_iter_at_line(b, &a, first);
	gtk_text_buffer_get_iter_at_line(b, &z, last + 1);
	if (gtk_text_iter_get_line(&z) == last) gtk_text_buffer_get_end_iter(b, &z);
	gtk_text_buffer_delete(b, &a, &z);
	gtk_text_buffer_create_mark(b, "edit", &a, TRUE);
	gtk_text_buffer_insert(b, &a, s->str, s->len);
	/* Right gravity: what is typed at the end of the last line stays before the mark, in the block */
	gtk_text_iter_backward_char(&a);
	gtk_text_buffer_create_mark(b, "edit-end", &a, FALSE);
	g_string_free(s, TRUE);
	gtk_text_buffer_get_start_iter(b, &x);
	gtk_text_buffer_get_iter_at_mark(b, &z, gtk_text_buffer_get_mark(b, "edit"));
	gtk_text_buffer_apply_tag_by_name(b, "lock", &x, &z);
	gtk_text_buffer_get_iter_at_mark(b, &x, gtk_text_buffer_get_mark(b, "edit-end"));
	gtk_text_buffer_get_end_iter(b, &z);
	gtk_text_buffer_apply_tag_by_name(b, "lock", &x, &z);
	recolor(b);
	g_object_set(view, "editable", TRUE, "cursor-visible", TRUE, NULL);
	gtk_text_buffer_get_iter_at_mark(b, &a, gtk_text_buffer_get_mark(b, "edit"));
	gtk_text_buffer_place_cursor(b, &a);
	gtk_widget_grab_focus(view);
}

/* The block as typed, without its last newline */
char *diff_edited(GtkWidget *view)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter a, z;
	gtk_text_buffer_get_iter_at_mark(b, &a, gtk_text_buffer_get_mark(b, "edit"));
	gtk_text_buffer_get_iter_at_mark(b, &z, gtk_text_buffer_get_mark(b, "edit-end"));
	return gtk_text_iter_get_text(&a, &z);
}
