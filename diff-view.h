
GtkWidget *diff_view(void (*hunk)(int from, int to), void (*menu)(GMenu *m, int hunk), void (*top)(const char *path));
void diff_counts(GtkTextBuffer *buffer, GtkTextIter *iter, long add, long del);
GtkWidget *diff_tools(GtkWidget *view);
void diff_files(GtkWidget *view, const char *text);
int diff_fold(GtkWidget *view, const char *dir, int len, int hide);
void diff_goto(GtkWidget *view, const char *path);
int diff_lines(GtkWidget *view, int *from, int *to);
void diff_keep(GtkWidget *view, double value);
double diff_top(GtkWidget *view);
void diff_edit(GtkWidget *view, int from, int to);
char *diff_edited(GtkWidget *view);

