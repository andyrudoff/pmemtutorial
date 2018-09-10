/*
 * freq_mt.c -- multi-threaded word frequency counter
 */
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NBUCKETS 10007

/* entries in a bucket are a linked list of struct entry */
struct entry {
	struct entry *next;
	const char *word;
	pthread_mutex_t mtx;	/* protects count field */
	int count;
};

/* each bucket contains a pointer to the linked list of entries */
struct bucket {
	pthread_mutex_t mtx;	/* protects entries field */
	struct entry *entries;
} H[NBUCKETS];


/* hash a string into an index into H[] */
unsigned hash(const char *s)
{
	unsigned h = NBUCKETS ^ ((unsigned)*s++ << 2);
	unsigned len = 0;

	while (*s) {
		len++;
		h ^= (((unsigned)*s) << (len % 3)) +
		    ((unsigned)*(s - 1) << ((len % 3 + 7)));
		s++;
	}
	h ^= len;

	return h % NBUCKETS;
}

/* bump the count for a word */
void count(const char *word)
{
	unsigned h = hash(word);

	/* grab bucket lock for bucket search */
	pthread_mutex_lock(&H[h].mtx);

	struct entry *ep = H[h].entries;

	for (; ep != NULL; ep = ep->next)
		if (strcmp(word, ep->word) == 0) {
			/* already in table, just bump the count */

			/* drop bucket lock */
			pthread_mutex_unlock(&H[h].mtx);

			/* lock the entry and update it */
			pthread_mutex_lock(&ep->mtx);
			ep->count++;
			pthread_mutex_unlock(&ep->mtx);
			return;
		}

	/* allocate new entry in table */
	if ((ep = calloc(1, sizeof(*ep))) == NULL)
		err(1, "calloc");

	if ((ep->word = strdup(word)) == NULL)
		err(1, "strdup");

	ep->count = 1;

	/* add it to the front of the linked list */
	ep->next = H[h].entries;
	H[h].entries = ep;
	pthread_mutex_init(&ep->mtx, NULL);

	pthread_mutex_unlock(&H[h].mtx);
}

#define MAXWORD 8192

/* break a test file into words and call count() on each one */
void *count_all_words(void *arg)
{
	const char *fname = (const char *)arg;
	FILE *fp;
	int c;
	char word[MAXWORD];
	char *ptr;

	if ((fp = fopen(fname, "r")) == NULL)
		err(1, "fopen: %s", fname);

	ptr = NULL;
	while ((c = getc(fp)) != EOF)
		if (isalpha(c)) {
			if (ptr == NULL) {
				/* starting a new word */
				ptr = word;
				*ptr++ = c;
			} else if (ptr < &word[MAXWORD - 1])
				/* add character to current word */
				*ptr++ = c;
			else {
				/* word too long, truncate it */
				*ptr++ = '\0';
				count(word);
				ptr = NULL;
			}
		} else if (ptr != NULL) {
			/* word ended, store it */
			*ptr++ = '\0';
			count(word);
			ptr = NULL;
		}

	/* handle the last word */
	if (ptr != NULL) {
		/* word ended, store it */
		*ptr++ = '\0';
		count(word);
	}

	fclose(fp);
	return NULL;
}

/* print all entries in the hash table */
void print_counts()
{
	struct entry *ep;

	for (int i = 0; i < NBUCKETS; i++)
		for (ep = H[i].entries; ep != NULL; ep = ep->next)
			printf("%d %s\n", ep->count, ep->word);
}

int main(int argc, char *argv[])
{
	int pflag = 0;
	int arg = 1;	/* index into argv[] for first file name */

	if (argc > 1 && strcmp(argv[1], "-p") == 0) {
		pflag++;
		arg++;
	}

	if (argv[arg] == NULL) {
		fprintf(stderr, "usage: %s [-p] wordfiles...\n", argv[0]);
		exit(1);
	}

	int nfiles = argc - arg;
	pthread_t tids[nfiles];

	for (int i = 0; i < nfiles; i++)
		if ((errno = pthread_create(&tids[i], NULL,
				count_all_words, (void *)argv[arg++])) != 0)
			err(1, "pthread_create %d of %d", i, nfiles);

	for (int i = 0; i < nfiles; i++)
		pthread_join(tids[i], NULL);

	if (pflag)
		print_counts();

	exit(0);
}
