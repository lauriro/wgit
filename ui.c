
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include "git.h"
#include "ui.h"

GtkWidget *win;
static GtkWidget *stack, *pane[4], *(*view[])(GtkWidget **) = {commit_view, log_view};
static int geo[] = {1000, 700, 300, 0, 450, 190};
static char *conf;

GtkWidget *scroll(GtkWidget *w)
{
	GtkWidget *s = gtk_scrolled_window_new(0, 0);
	gtk_container_add(GTK_CONTAINER(s), w);
	return s;
}

static gboolean quit(GtkWidget *w)
{
	FILE *f;
	int k;
	if (!(gdk_window_get_state(gtk_widget_get_window(w)) & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)))
		gtk_window_get_size(GTK_WINDOW(w), geo, geo + 1);
	for (k = 0; k < 4; k++) if (pane[k & 2]) geo[k + 2] = gtk_paned_get_position(GTK_PANED(pane[k]));
	g_mkdir_with_parents(g_get_user_state_dir(), 0700);
	if ((f = fopen(conf, "w"))) {
		fprintf(f, "%d %d %d %d %d %d\n", geo[0], geo[1], geo[2], geo[3], geo[4], geo[5]);
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

void flip(gpointer v)
{
	int i = 2 * GPOINTER_TO_INT(v), k, built = !!pane[i];
	GtkWidget *w = view[i / 2](pane + i);
	if (!built) {
		for (k = i; k < i + 2; k++) if (geo[k + 2]) gtk_paned_set_position(GTK_PANED(pane[k]), geo[k + 2]);
		gtk_container_add(GTK_CONTAINER(stack), w);
		gtk_widget_show_all(w);
	}
	gtk_stack_set_visible_child(GTK_STACK(stack), w);
}

int app(const char *path, int v)
{
	FILE *f;
	char *top = repo(path);
	if (!top) return fputs("wgit: not a git repository\n", stderr), 1;
	if (v < 0) status(&v), v = !v;
	setenv("NO_AT_BRIDGE", "1", 0);
	g_set_prgname("wgit");
	gtk_init(0, 0);
	theme();
	if ((f = fopen(conf = g_build_filename(g_get_user_state_dir(), "wgit", NULL), "r"))) {
		fscanf(f, "%d %d %d %d %d %d", geo, geo + 1, geo + 2, geo + 3, geo + 4, geo + 5);
		fclose(f);
	}
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), top);
	gtk_window_set_default_size(GTK_WINDOW(win), geo[0], geo[1]);
	gtk_container_add(GTK_CONTAINER(win), stack = gtk_stack_new());
	g_signal_connect(win, "delete-event", G_CALLBACK(quit), 0);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), 0);
	flip(GINT_TO_POINTER(v));
	gtk_widget_show_all(win);
	gtk_main();
	return 0;
}

