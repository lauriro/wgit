
extern GtkWidget *win;

GtkWidget *scroll(GtkWidget *w);
GtkWidget *split(int orientation, GtkWidget *a, GtkWidget *b, int first);
GtkWidget *margin(GtkWidget *w);
void events(GtkWidget *w, GCallback fn, gpointer data);
void local(GtkWidget *w, GdkEvent *e, double *x, double *y);
void actions(GtkWidget *w, const char *const *names, const GCallback *fns, int n);
void alert(const char *s);
GtkWidget *tree(GtkTreeModel *m);
GtkWidget *commit_view(GtkWidget **pane);
GtkWidget *log_view(GtkWidget **pane);
GtkWidget *stat_view(GtkWidget **pane);
GtkWidget *tabs(GtkWidget *w, GtkWidget *tools);
int app(const char *path, int view);

