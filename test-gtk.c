#define stats fixture_stats
#include "stat.c"
#undef stats

char *fixture_stats(int days, long now, const char *path)
{
	return strdup("Commits: 3\t+139-104\n\n"
		"     1  33.3%  Üser <a+b@t>\t+1\n"
		"     1  33.3%  file-name\t-2\n"
		"     1  33.3%  src/a+b.c\t+3-4\n"
		"     1  33.3%  report +0\n"
		"     1  33.3%  asset -12\n"
		"Files added/deleted: 0\n");
}

int main(int argc, char **argv)
{
	GtkTextIter a, b;
	GtkTextTag *t;
	char *text, *face, *actual;
	gtk_init(&argc, &argv);
	stack = gtk_stack_new();
	page[0] = gtk_label_new("page");
	gtk_container_add(GTK_CONTAINER(stack), page[0]);
	gtk_widget_show_all(stack);
	list = gtk_tree_view_new();
	where = gtk_label_new("");
	buf = gtk_text_buffer_new(0);
	load();
	gtk_text_buffer_get_bounds(buf, &a, &b);
	text = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
	g_assert_cmpstr(text, ==, "Commits: 3 +139\u2006-104\n\n"
		"     1  33.3%  Üser <a+b@t> +1\u2006\n"
		"     1  33.3%  file-name -2\n"
		"     1  33.3%  src/a+b.c +3\u2006-4\n"
		"     1  33.3%  report +0\n"
		"     1  33.3%  asset -12\n"
		"Files added/deleted: 0\n");
	g_free(text);
	gtk_text_buffer_get_iter_at_offset(buf, &a, 11);
	t = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buf), "+");
	g_assert(gtk_text_iter_has_tag(&a, t));
	gtk_text_buffer_get_iter_at_offset(buf, &a, 16);
	t = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buf), "-");
	g_assert(gtk_text_iter_has_tag(&a, t));
	gtk_text_buffer_get_start_iter(buf, &a);
	g_assert(gtk_text_iter_forward_search(&a, "-12", 0, &a, &b, NULL));
	g_assert(!gtk_text_iter_has_tag(&a, t));
	t = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buf), "counts");
	g_object_get(t, "font", &actual, NULL);
	g_object_get(gtk_settings_get_default(), "gtk-font-name", &face, NULL);
	g_assert_cmpstr(actual, ==, face);
	g_free(actual);
	g_free(face);
	g_print("GTK Stat text, color tags and count font passed\n");
	/* An edit block: its lines without their marks, typing only there, a new diff ends it. Opening it once
	 * crashed, the text inserted before its end was marked */
	GtkWidget *d = diff_view(0, 0, 0);
	char *edited;
	diff_files(d, "diff --git a/f b/f\n--- a/f\n+++ b/f\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n");
	diff_edit(d, 6, 7);
	g_assert(gtk_text_view_get_editable(GTK_TEXT_VIEW(d)));
	g_assert_cmpstr(edited = diff_edited(d), ==, "B\nc");
	g_free(edited);
	diff_files(d, "");
	g_assert(!gtk_text_view_get_editable(GTK_TEXT_VIEW(d)));
	g_print("GTK diff edit block passed\n");
	return 0;
}
