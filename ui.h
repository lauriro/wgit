
extern GtkWidget *win;

GtkWidget *scroll(GtkWidget *w);
GtkWidget *commit_view(GtkWidget **pane);
GtkWidget *log_view(GtkWidget **pane);
GtkWidget *stat_view(GtkWidget **pane);
GtkWidget *tabs(GtkWidget *w, GtkWidget *tools);
int app(const char *path, int view);

