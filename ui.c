
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "git.h"
#include "ui.h"

GtkWidget *win;
static GtkWidget *stack, *page[3], *pane[6], *(*view[])(GtkWidget **) = {commit_view, log_view, stat_view};
static const char *title[] = {"Commit", "Log", "Stat"};
static int geo[] = {1000, 700, 300, 0, 450, 0, 250, 0};
static char *conf;

GtkWidget *scroll(GtkWidget *w)
{
	return g_object_new(GTK_TYPE_SCROLLED_WINDOW, "child", w, "vexpand", TRUE, NULL);
}

/* A wide handle: the usual line of 1 px is hard to grab. With first, the first child takes the change of size,
 * else the second does and may shrink */
GtkWidget *split(int orientation, GtkWidget *a, GtkWidget *b, int first)
{
	return g_object_new(GTK_TYPE_PANED, "orientation", orientation, "wide-handle", TRUE, "start-child", a, "end-child", b,
		"resize-start-child", first, "shrink-start-child", FALSE, "resize-end-child", !first, "shrink-end-child", !first, NULL);
}

/* GTK 4 has no border width: a margin all round does its work */
GtkWidget *margin(GtkWidget *w)
{
	g_object_set(w, "margin-start", 6, "margin-end", 6, "margin-top", 6, "margin-bottom", 6, NULL);
	return w;
}

/* The raw events before the widget's own handling, as the GTK 3 event signals had them: a gesture of the widget
 * that claims a press would take the release from a gesture of ours */
void events(GtkWidget *w, GCallback fn, gpointer data)
{
	GtkEventController *c = gtk_event_controller_legacy_new();
	gtk_event_controller_set_propagation_phase(c, GTK_PHASE_CAPTURE);
	g_signal_connect(c, "event", fn, data);
	gtk_widget_add_controller(w, c);
}

/* A raw event has its place on the surface */
void local(GtkWidget *w, GdkEvent *e, double *x, double *y)
{
	GtkNative *n = gtk_widget_get_native(w);
	graphene_point_t p;
	double nx, ny;
	gdk_event_get_position(e, x, y);
	gtk_native_get_surface_transform(n, &nx, &ny);
	if (gtk_widget_compute_point(GTK_WIDGET(n), w, &GRAPHENE_POINT_INIT(*x - nx, *y - ny), &p)) *x = p.x, *y = p.y;
}

static void fire(gpointer fn, GVariant *v)
{
	((void (*)(gpointer))fn)(GINT_TO_POINTER(g_variant_get_int32(v)));
}

/* A menu item names its action and an int for it, "v.name(1)". "v.no(0)" is an item that cannot be used:
 * a menu model has no sensitivity of its own */
void actions(GtkWidget *w, const char *const *names, const GCallback *fns, int n)
{
	GSimpleActionGroup *g = g_simple_action_group_new();
	GSimpleAction *a;
	int k;
	for (k = 0; k <= n; k++) {
		a = g_simple_action_new(k < n ? names[k] : "no", G_VARIANT_TYPE_INT32);
		if (k < n) g_signal_connect_swapped(a, "activate", G_CALLBACK(fire), fns[k]);
		else g_simple_action_set_enabled(a, FALSE);
		g_action_map_add_action(G_ACTION_MAP(g), G_ACTION(a));
		g_object_unref(a);
	}
	gtk_widget_insert_action_group(w, "v", G_ACTION_GROUP(g));
	g_object_unref(g);
}

void alert(const char *s)
{
	GtkAlertDialog *d = gtk_alert_dialog_new("%s", s);
	gtk_alert_dialog_show(d, GTK_WINDOW(win));
	g_object_unref(d);
}

static void arrow(GtkTreeView *v, GtkTreeIter *it, GtkTreePath *p, gpointer shut)
{
	gtk_tree_store_set(GTK_TREE_STORE(gtk_tree_view_get_model(v)), it, 2, shut ? "pan-end-symbolic" : "pan-down-symbolic", -1);
}

/* A tree with the arrows of the diff titles. Column 0 of the store is the markup, 2 the icon name, 3 if the icon
 * is active. The arrow and the name are in one column, so the name follows the indentation of its level */
GtkWidget *tree(GtkTreeModel *m)
{
	GtkWidget *v = g_object_new(GTK_TYPE_TREE_VIEW, "model", m, "headers-visible", FALSE, "show-expanders", FALSE, "level-indentation", 16, NULL);
	GtkTreeViewColumn *col = gtk_tree_view_column_new();
	GtkCellRenderer *r = g_object_new(GTK_TYPE_CELL_RENDERER_PIXBUF, "width", 20, "xpad", 4, "xalign", 0., NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(v), col);
	gtk_tree_view_column_pack_start(col, r, FALSE);
	gtk_tree_view_column_set_attributes(col, r, "icon-name", 2, "sensitive", 3, NULL);
	gtk_tree_view_column_pack_start(col, r = gtk_cell_renderer_text_new(), TRUE);
	gtk_tree_view_column_set_attributes(col, r, "markup", 0, NULL);
	g_signal_connect(v, "row-collapsed", G_CALLBACK(arrow), GINT_TO_POINTER(1));
	g_signal_connect(v, "row-expanded", G_CALLBACK(arrow), 0);
	return v;
}

/* GTK 4 keeps the default size of a tiled window as it was, so the size is the window's own */
static gboolean quit(GtkWindow *w)
{
	FILE *f;
	int k;
	if (!gtk_window_is_maximized(w) && !gtk_window_is_fullscreen(w))
		geo[0] = gtk_widget_get_width(GTK_WIDGET(w)), geo[1] = gtk_widget_get_height(GTK_WIDGET(w));
	for (k = 0; k < 5; k++) if (pane[k & 6]) g_object_get(pane[k], "position", geo + k + 2, NULL);
	g_mkdir_with_parents(g_get_user_state_dir(), 0700);
	if ((f = fopen(conf, "w"))) {
		fprintf(f, "%d %d %d %d %d %d %d\n", geo[0], geo[1], geo[2], geo[3], geo[4], geo[5], geo[6]);
		fclose(f);
	}
	return FALSE;
}

static void dark(GVariant *v)
{
	GVariant *u;
	while (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
		u = g_variant_get_variant(v);
		g_variant_unref(v);
		v = u;
	}
	if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32))
		/* The desktop theme's dark one by its own name: GTK 4 takes a named theme's dark part only by a setting it warns of,
		 * and its own colour scheme set at all put its own theme in our theme's place */
		g_object_set(gtk_settings_get_default(), "gtk-theme-name", g_variant_get_uint32(v) == 1 ? "Desk-dark" : "Desk", NULL);
	g_variant_unref(v);
}

static void changed(GDBusConnection *c, const char *from, const char *path, const char *iface, const char *sig, GVariant *p)
{
	const char *k;
	g_variant_get_child(p, 1, "&s", &k);
	if (!strcmp(k, "color-scheme")) dark(g_variant_get_child_value(p, 2));
}

/* GTK reads the portal with ReadAll, which tark-shell does not have */
static void theme(void)
{
	static const char *how[] = {"ReadOne", "Read"};
	const char *name = "org.freedesktop.portal.Desktop", *path = "/org/freedesktop/portal/desktop";
	const char *iface = "org.freedesktop.portal.Settings", *ns = "org.freedesktop.appearance";
	GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, 0, 0);
	GVariant *r = 0;
	int k;
	if (!bus) return;
	for (k = 0; !r && k < 2; k++)
		r = g_dbus_connection_call_sync(bus, name, path, iface, how[k], g_variant_new("(ss)", ns, "color-scheme"), 0, 0, 1000, 0, 0);
	if (r) {
		dark(g_variant_get_child_value(r, 0));
		g_variant_unref(r);
	}
	g_dbus_connection_signal_subscribe(bus, name, iface, "SettingChanged", path, ns, 0, (GDBusSignalCallback)changed, 0, 0);
}

static void show(int v)
{
	int i = 2 * v, k, built = !!pane[i];
	GtkWidget *w = view[v](pane + i);
	if (built) return;
	for (k = i; k < i + 2; k++) if (geo[k + 2]) g_object_set(pane[k], "position", geo[k + 2], NULL);
	gtk_widget_set_vexpand(w, TRUE);
	gtk_box_append(GTK_BOX(page[v]), w);
}

static void switched(GtkStack *s)
{
	int k = 0;
	while (page[k] != gtk_stack_get_visible_child(s)) k++;
	show(k);
}

/* The view's tools on the left, the views and a × on the right.
 * The window has no title bar under dwl: the × is its close, through close-request, so the sizes are saved.
 * A click leaves the focus where it was, so no focus ring is left around it */
GtkWidget *tabs(GtkWidget *w, GtkWidget *tools)
{
	GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), *h = margin(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6)),
		*x = g_object_new(GTK_TYPE_BUTTON, "icon-name", "window-close-symbolic", "valign", GTK_ALIGN_CENTER, "focus-on-click", FALSE, "css-classes", (const char *[]){ "close", 0 }, NULL);
	g_signal_connect_swapped(x, "clicked", G_CALLBACK(gtk_window_close), win);
	gtk_box_append(GTK_BOX(h), tools);
	gtk_box_append(GTK_BOX(h), g_object_new(GTK_TYPE_STACK_SWITCHER, "stack", stack, "hexpand", TRUE, "halign", GTK_ALIGN_END, NULL));
	gtk_box_append(GTK_BOX(h), x);
	gtk_box_append(GTK_BOX(b), h);
	gtk_widget_set_vexpand(w, TRUE);
	gtk_box_append(GTK_BOX(b), w);
	return b;
}

int app(const char *path, int v)
{
	FILE *f;
	GtkCssProvider *css;
	GMainLoop *loop;
	char *top = repo(path);
	int i;
	if (!top) return fputs("gk: not a git repository\n", stderr), 1;
	if (v < 0) status(), v = !nent;
	setenv("GTK_A11Y", "none", 0);
	g_set_prgname("gk");
	gtk_init();
	theme();
	gtk_css_provider_load_from_string(css = gtk_css_provider_new(), "stackswitcher button { min-width: 0; } #plain, #plain text { background: none; }");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	if ((f = fopen(conf = g_strdup_printf("%s/gk", g_get_user_state_dir()), "r"))) {
		fscanf(f, "%d %d %d %d %d %d %d", geo, geo + 1, geo + 2, geo + 3, geo + 4, geo + 5, geo + 6);
		fclose(f);
	}
	win = g_object_new(GTK_TYPE_WINDOW, "title", top, "default-width", geo[0], "default-height", geo[1], "child", stack = gtk_stack_new(), NULL);
	for (i = 0; i < 3; i++) gtk_stack_add_titled(GTK_STACK(stack), page[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), title[i], title[i]);
	g_object_set(stack, "visible-child", page[v], NULL);
	show(v);
	g_signal_connect(stack, "notify::visible-child", G_CALLBACK(switched), 0);
	g_signal_connect(win, "close-request", G_CALLBACK(quit), 0);
	g_signal_connect_swapped(win, "destroy", G_CALLBACK(g_main_loop_quit), loop = g_main_loop_new(0, 0));
	gtk_window_present(GTK_WINDOW(win));
	g_main_loop_run(loop);
	return 0;
}

