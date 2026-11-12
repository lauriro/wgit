
GtkWidget *diff_view(void (*hunk)(int from, int to), void (*menu)(GtkMenu *m, int hunk, int line));
void diff_show(const char *text);
int diff_lines(int *from, int *to);

