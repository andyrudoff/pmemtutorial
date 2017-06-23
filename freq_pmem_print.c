/*
 * freq_pmem_print.c -- print word frequency counts from pmem file
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
	PMEMmutex mutex;		/* protects count field */
	int count;
};

/* each bucket contains a pointer to the linked list of entries */
struct bucket {
	PMEMrwlock rwlock;		/* protects entries field */
	TOID(struct entry) entries;
};

PMEMobjpool *Pop;	/* pmemobj pool pointer */
struct bucket *H;	/* run-time pointer to H[] in pmem */


/* print all entries in the hash table */
void print_counts()
{
	for (int i = 0; i < NBUCKETS; i++) {
		TOID(struct entry) ep = H[i].entries;

		for (; !TOID_IS_NULL(ep); ep = D_RW(ep)->next)
			printf("%d %s\n", D_RO(ep)->count,
						D_RO(D_RO(ep)->word));
	}
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s pmemfile\n", argv[0]);
		exit(1);
	}

	Pop = pmemobj_open(argv[1], POBJ_LAYOUT_NAME(freq));

	if (Pop == NULL)
		err(1, "pmemobj_open: %s", argv[1]);

	TOID(struct root) root = POBJ_ROOT(Pop, struct root);

	/* before starting, see if buckets have been allocated */
	if (TOID_IS_NULL(D_RW(root)->h)) {
		/* nope, treat like empty table */
		exit(0);
	}

	/* get run-time pointer to hash table */
	H = D_RW(D_RW(root)->h);

	print_counts();

	pmemobj_close(Pop);
	exit(0);
}
