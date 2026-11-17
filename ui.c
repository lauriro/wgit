
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
	GtkWidget *s = gtk_scrolled_window_new(0, 0);
	gtk_container_add(GTK_CONTAINER(s), w);
	return s;
}

/* A wide handle: the usual line of 1 px is hard to grab */
GtkWidget *split(int orientation)
{
	return g_object_new(GTK_TYPE_PANED, "orientation", orientation, "wide-handle", TRUE, NULL);
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

static gboolean quit(GtkWidget *w)
{
	FILE *f;
	int k;
	if (!(gdk_window_get_state(gtk_widget_get_window(w)) & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)))
		gtk_window_get_size(GTK_WINDOW(w), geo, geo + 1);
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
		g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", g_variant_get_uint32(v) == 1, NULL);
	g_variant_unref(v);
}

static void changed(GDBusConnection *c, const char *from, const char *path, const char *iface, const char *sig, GVariant *p)
{
	const char *k;
	g_variant_get_child(p, 1, "&s", &k);
	if (!strcmp(k, "color-scheme")) dark(g_variant_get_child_value(p, 2));
}

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
	gtk_box_pack_start(GTK_BOX(page[v]), w, TRUE, TRUE, 0);
	gtk_widget_show_all(w);
}

static void switched(GtkStack *s)
{
	int k = 0;
	while (page[k] != gtk_stack_get_visible_child(s)) k++;
	show(k);
}

/* The view's tools on the left, the views and a × on the right.
 * The window has no title bar under dwl: the × is its close, through delete-event, so the sizes are saved.
 * Its look is the desktop theme's .close, a plain flat button elsewhere; a click leaves the focus where it was,
 * so no focus ring is left around it */
GtkWidget *tabs(GtkWidget *w, GtkWidget *tools)
{
	GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *s = g_object_new(GTK_TYPE_STACK_SWITCHER, "stack", stack, NULL),
		*x = g_object_new(GTK_TYPE_BUTTON, "image", gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU), "valign", GTK_ALIGN_CENTER, "focus-on-click", FALSE, NULL);
	GtkStyleContext *c = gtk_widget_get_style_context(x);
	gtk_style_context_add_class(c, "close");
	gtk_style_context_add_class(c, "flat");
	g_signal_connect_swapped(x, "clicked", G_CALLBACK(gtk_window_close), win);
	gtk_container_set_border_width(GTK_CONTAINER(h), 6);
	gtk_box_pack_start(GTK_BOX(h), tools, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(h), x, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(h), s, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(b), h, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(b), w, TRUE, TRUE, 0);
	return b;
}

int app(const char *path, int v)
{
	FILE *f;
	GtkCssProvider *css;
	char *top = repo(path);
	int i;
	if (!top) return fputs("wgit: not a git repository\n", stderr), 1;
	if (v < 0) status(), v = !nent;
	setenv("NO_AT_BRIDGE", "1", 0);
	g_set_prgname("wgit");
	gtk_init(0, 0);
	theme();
	gtk_css_provider_load_from_data(css = gtk_css_provider_new(), "stackswitcher button { min-width: 0 } #plain, #plain text { background: none }", -1, 0);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), (GtkStyleProvider *)css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	if ((f = fopen(conf = g_strdup_printf("%s/wgit", g_get_user_state_dir()), "r"))) {
		fscanf(f, "%d %d %d %d %d %d %d", geo, geo + 1, geo + 2, geo + 3, geo + 4, geo + 5, geo + 6);
		fclose(f);
	}
	win = g_object_new(GTK_TYPE_WINDOW, "title", top, "default-width", geo[0], "default-height", geo[1], NULL);
	gtk_container_add(GTK_CONTAINER(win), stack = gtk_stack_new());
	for (i = 0; i < 3; i++) {
		gtk_widget_show_all(page[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
		gtk_stack_add_titled(GTK_STACK(stack), page[i], title[i], title[i]);
	}
	g_object_set(stack, "visible-child", page[v], NULL);
	show(v);
	g_signal_connect(stack, "notify::visible-child", G_CALLBACK(switched), 0);
	g_signal_connect(win, "delete-event", G_CALLBACK(quit), 0);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), 0);
	gtk_widget_show_all(win);
	gtk_main();
	return 0;
}

