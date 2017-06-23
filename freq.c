/*
 * freq.c -- simple word frequency counter
 */
#include <ctype.h>
#include <err.h>
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
	int count;
};

/* each bucket contains a pointer to the linked list of entries */
struct bucket {
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
	struct entry *ep = H[h].entries;

	for (; ep != NULL; ep = ep->next)
		if (strcmp(word, ep->word) == 0) {
			/* already in table, just bump the count */

			ep->count++;

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
}

#define MAXWORD 8192

/* break a test file into words and call count() on each one */
void count_all_words(const char *fname)
{
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

	for (; arg < argc; arg++)
		count_all_words(argv[arg]);

	if (pflag)
		print_counts();

	exit(0);
}
