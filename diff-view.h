
GtkWidget *diff_view(void (*hunk)(int from, int to), void (*menu)(GtkMenu *m, int hunk), void (*top)(const char *path));
GtkWidget *diff_tools(GtkWidget *view);
void diff_files(GtkWidget *view, const char *text);
void diff_fold(GtkWidget *view, int hide);
void diff_goto(GtkWidget *view, const char *path);
int diff_lines(GtkWidget *view, int *from, int *to);

