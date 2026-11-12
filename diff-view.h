
GtkWidget *diff_view(void (*hunk)(int from, int to), void (*menu)(GtkMenu *m, int hunk, int line));
void diff_show(GtkWidget *view, const char *text);
int diff_lines(GtkWidget *view, int *from, int *to);

