
typedef struct {
	char x, y, *path, *orig;
} Entry;

extern char *base;

int git(char **out, const char *in, char **argv);
char *parent(void);
char *repo(const char *path);
Entry *status(int *n);
char *diff(Entry *e, int cached);
char *patch(const char *d, int from, int to, int rev);
int apply(char **out, const char *d, int from, int to, int rev);
int files(char **out, int op, Entry **e, int n);
char *patterns(Entry **e, int n);
int ignore(char **out, char *add, int sort);
int commit(char **out, const char *msg, int amend);

