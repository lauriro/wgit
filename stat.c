
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "git.h"
#include "ui.h"

static const char name[][11] = {"Last month", "Last year", "All"};
static const int days[] = {30, 365, 0};
static GtkWidget *stack, *page[3];
static GtkTextBuffer *buf;

static void load(void)
{
	GtkTextIter it;
	char *o, *s, *p, *e;
	int b, k = 0;
	while (page[k] != gtk_stack_get_visible_child(GTK_STACK(stack))) k++;
	s = g_utf8_make_valid(o = stats(days[k], time(0)), -1);
	gtk_text_buffer_set_text(buf, "", 0);
	for (p = s; *p; p = e) {
		b = !strncmp(p, "\xe2\x96", 2);
		for (e = p; *e && b == !strncmp(e, "\xe2\x96", 2); e += 1 + 2 * b);
		gtk_text_buffer_get_end_iter(buf, &it);
		gtk_text_buffer_insert_with_tags_by_name(buf, &it, p, e - p, b ? "bar" : NULL, NULL);
	}
	free(o);
	g_free(s);
}

GtkWidget *stat_view(GtkWidget **pane)
{
	GtkWidget *v;
	int k;
	if (!pane[0]) {
		stack = gtk_stack_new();
		for (k = 0; k < 3; k++) {
			v = g_object_new(GTK_TYPE_TEXT_VIEW, "editable", FALSE, "cursor-visible", FALSE, "monospace", TRUE,
				"left-margin", 8, "top-margin", 8, buf ? "buffer" : NULL, buf, NULL);
			buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v));
			gtk_stack_add_titled(GTK_STACK(stack), page[k] = scroll(v), name[k], name[k]);
		}
		gtk_text_buffer_create_tag(buf, "bar", "foreground", "#4a87e8", NULL);
		g_signal_connect(stack, "notify::visible-child", G_CALLBACK(load), 0);
		pane[0] = tabs(stack, g_object_new(GTK_TYPE_STACK_SWITCHER, "stack", stack, NULL));
	}
	load();
	return pane[0];
}
