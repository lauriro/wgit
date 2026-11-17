
typedef struct {
	char *path, *orig, x, y;
} Entry;

typedef struct {
	char *graph, *hash, *full, *refs, *author, *date, *subject;
	int note, edge;
} Row;

extern Entry *ent;
extern char *base;
extern int context, nent;

int git(char **out, const char *in, char **argv);
char *parent(void);
char *draft(void);
void keep(const char *text);
char *repo(const char *path);
void status(void);
char *diff(Entry *e, int cached);
char *diffs(int sec, Entry *e, int n);
char *patch(const char *d, int from, int to, int rev);
int apply(char **out, const char *d, int from, int to, int mode);
int files(char **out, int op, Entry **e, int n);
char *patterns(Entry **e, int n);
int ignore(char **out, char *add, int sort);
int commit(char **out, const char *msg, int amend);
Row *history(int *n, int max);
int joins(const char *g, int k, int down);
char *stats(int days, long now, const char *path);

