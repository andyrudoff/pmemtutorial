/*
 * freq_pmem.c -- pmem-based word frequency counter
 *
 * create the pool for this program using pmempool, for example:
 *	pmempool create obj --layout=freq -s 1G freqcount
 *	freq_pmem freqcount file1.txt file2.txt...
 */
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <libpmemobj.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* declare all the types used in the layout of our pmempool file */
POBJ_LAYOUT_BEGIN(freq);
POBJ_LAYOUT_ROOT(freq, struct root);
POBJ_LAYOUT_TOID(freq, struct entry);
POBJ_LAYOUT_TOID(freq, char);
POBJ_LAYOUT_TOID(freq, struct bucket);
POBJ_LAYOUT_END(freq);

/* root object definition */
struct root {
	TOID(struct bucket) h;	/* hash table for word frequencies */
	/* ... OIDs for other things we store in this pool go here... */
};

#define NBUCKETS 10007

/* entries in a bucket are a linked list of struct entry */
struct entry {
	TOID(struct entry) next;
	TOID(char) word;
	PMEMmutex mtx;		/* protects count field */
	int count;
};

/* each bucket contains a pointer to the linked list of entries */
struct bucket {
	PMEMmutex mtx;		/* protects entries field */
	TOID(struct entry) entries;
};

PMEMobjpool *Pop;	/* pmemobj pool pointer */
struct bucket *H;	/* run-time pointer to H[] in pmem */

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
	pmemobj_mutex_lock(Pop, &H[h].mtx);

	TOID(struct entry) ep = H[h].entries;

	for (; !TOID_IS_NULL(ep); ep = D_RW(ep)->next)
		if (strcmp(word, D_RO(D_RO(ep)->word)) == 0) {
			/* already in table, just bump the count */

			/* drop bucket lock */
			pmemobj_mutex_unlock(Pop, &H[h].mtx);

			/* lock entry and update it transactionally */
			TX_BEGIN_PARAM(Pop,
					TX_PARAM_MUTEX, &D_RW(ep)->mtx,
					TX_PARAM_NONE) {

				TX_ADD(ep);
				D_RW(ep)->count++;

			} TX_ONABORT {
				err(1, "can't bump count for \"%s\"", word);
			} TX_END

			return;
		}

	/* allocate new entry in table */
	TX_BEGIN(Pop) {

		/* add field being changed to transaction */
		pmemobj_tx_add_range_direct(&H[h].entries,
						sizeof(H[h].entries));

		/* allocate entry struct and fill it in */
		ep = TX_ZALLOC(struct entry, sizeof(struct entry));

		TOID_ASSIGN(D_RW(ep)->word,
					TX_STRDUP(word, TOID_TYPE_NUM(char)));

		D_RW(ep)->count = 1;

		/* add it to the front of the linked list */
		D_RW(ep)->next = H[h].entries;
		H[h].entries = ep;

	} TX_ONABORT {
		err(1, "can't create entry for \"%s\"", word);
	} TX_END

	pmemobj_mutex_unlock(Pop, &H[h].mtx);
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

int main(int argc, char *argv[])
{
	int arg = 2;	/* index into argv[] for first file name */

	if (argv[1] == NULL || argv[arg] == NULL) {
		fprintf(stderr, "usage: %s pmemfile wordfiles...\n", argv[0]);
		exit(1);
	}

	Pop = pmemobj_open(argv[1], POBJ_LAYOUT_NAME(freq));

	if (Pop == NULL)
		err(1, "pmemobj_open: %s", argv[1]);

	TOID(struct root) root = POBJ_ROOT(Pop, struct root);

	/* before starting, see if buckets have been allocated */
	if (TOID_IS_NULL(D_RW(root)->h)) {
		/* nope, allocate it now */
		TX_BEGIN(Pop) {
			TX_ADD(root);
			D_RW(root)->h = TX_ZALLOC(struct bucket,
			    sizeof(struct bucket) * NBUCKETS);
		} TX_ONABORT {
			err(1, "cannot allocate hash table");
		} TX_END
	}

	/* get run-time pointer to hash table */
	H = D_RW(D_RW(root)->h);

	int nfiles = argc - arg;
	pthread_t tids[nfiles];

	for (int i = 0; i < nfiles; i++)
		if ((errno = pthread_create(&tids[i], NULL,
				count_all_words, (void *)argv[arg++])) != 0)
			err(1, "pthread_create %d of %d", i, nfiles);

	for (int i = 0; i < nfiles; i++)
		pthread_join(tids[i], NULL);

	pmemobj_close(Pop);
	exit(0);
}
