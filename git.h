
typedef struct {
	char x, y, *path, *orig;
} Entry;

int git(char **out, const char *in, char **argv);
char *repo(const char *path);
Entry *status(int *n);
char *diff(Entry *e, int cached);
char *patch(const char *d, int from, int to, int rev);
int apply(char **out, const char *d, int from, int to, int rev);
int files(char **out, int op, Entry **e, int n);
int commit(char **out, const char *msg, int amend);

