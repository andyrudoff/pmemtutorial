/*
 * freq_pmem_cpp.cpp -- pmem-based word frequency counter, C++ version
 *
 * create the pool for this program using pmempool, for example:
 *	pmempool create obj --layout=freq -s 1G freqcount
 *	freq_pmem_cpp freqcount file1.txt file2.txt...
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

#include <iostream>
#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/transaction.hpp>

#define LAYOUT "freq"
#define NBUCKETS 10007

using nvml::obj::p;
using nvml::obj::persistent_ptr;
using nvml::obj::pool;
using nvml::obj::pool_base;
using nvml::obj::make_persistent;
using nvml::obj::delete_persistent;
using nvml::obj::transaction;

class freq {

	/* entries in a bucket are a linked list of struct entry */
	struct entry {
		persistent_ptr<struct entry> next;
		persistent_ptr<char> word;
		PMEMmutex mutex;		/* protects count field */
		p<int> count;
	};

	/* each bucket contains a pointer to the linked list of entries */
	struct bucket {
		PMEMrwlock rwlock;		/* protects entries field */
		persistent_ptr<struct entry> entries;
	};

public:
	/* hash a string into an index into h[] */
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
		/* XXX */
		std::cerr << "count: " << word << std::endl;
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

private:
	/* hash table for word frequencies */
	persistent_ptr<struct bucket *> h;
};

struct root {
	persistent_ptr<class freq> freq;	/* word frequencies */
	/* ... other things we store in this pool go here... */
};


int main(int argc, char *argv[])
{
	if (argc < 3) {
		std::cerr << "usage: " << argv[0]
			  << " pmemfile wordfiles..." << std::endl;
		exit(1);
	}

	auto pop = pool<root>::open(argv[1], LAYOUT);
	auto q = pop.get_root();

	/* before starting, see if buckets have been allocated */
	if (q->freq == nullptr) {
		/* XXX */
		std::cerr << "q->freq is nullptr" << std::endl;
	} else {
		std::cerr << "q->freq is NOT nullptr" << std::endl;
	}

	/* XXX */

	pop.close();
	exit(0);
}
