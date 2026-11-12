
extern GtkWidget *win;

GtkWidget *scroll(GtkWidget *w);
GtkWidget *commit_view(GtkWidget **pane);
GtkWidget *log_view(GtkWidget **pane);
void flip(gpointer view);
int app(const char *path, int view);

