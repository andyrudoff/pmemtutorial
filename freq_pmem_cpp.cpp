/*
 * freq_pmem_cpp.cpp -- pmem-based word frequency counter, C++ version
 *
 * create the pool for this program using pmempool, for example:
 *	pmempool create obj --layout=freq -s 1G freqcount
 *	freq_pmem_cpp freqcount file1.txt file2.txt...
 */
#include <cctype>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <iostream>
#include <thread>
#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/make_persistent_array.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/pext.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/transaction.hpp>
#include <libpmemobj++/mutex.hpp>
#include <libpmemobj++/shared_mutex.hpp>

#define LAYOUT "freq"
#define NBUCKETS 10007

using nvml::obj::p;
using nvml::obj::persistent_ptr;
using nvml::obj::pool;
using nvml::obj::pool_base;
using nvml::obj::make_persistent;
using nvml::obj::delete_persistent;
using nvml::obj::transaction;
using nvml::obj::mutex;
using nvml::obj::shared_mutex;

/* entries in a bucket are a linked list of struct entry */
struct entry {

	// XXX do we care about the type number for the strdup?
	entry(int ct, const char *wrd,
			const persistent_ptr<struct entry> &nxt) : next{nxt},
					word{pmemobj_tx_strdup(wrd,
					nvml::detail::type_num<char>())},
					count{ct}
	{}

	persistent_ptr<struct entry> next;
	persistent_ptr<char> word;
	mutex mutex;		/* protects count field */
	p<int> count;
};

/* each bucket contains a pointer to the linked list of entries */
struct bucket {
	shared_mutex rwlock;		/* protects entries field */
	persistent_ptr<struct entry> entries;
};

using buckets = persistent_ptr<bucket[NBUCKETS]>;

class freq {
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
		unsigned h = hash(word);

		auto &rwlock = ht[h].rwlock;
		rwlock.lock_shared();

		auto ep = ht[h].entries;

		for (; ep != nullptr; ep = ep->next)
			if (strcmp(word, ep->word.get()) == 0) {
				/* already in table, just bump the count */

				/* drop bucket read lock */
				rwlock.unlock_shared();

				/* lock and update transactionally */
				transaction::exec_tx(pop, [&ep]() {
					++ep->count;
				}, ep->mutex);
				return;
			}

		/* drop bucket read lock */
		rwlock.unlock_shared();

		/* allocate new entry in table */
		transaction::exec_tx(pop, [&ep, &word, this, &h]() {
			ep = make_persistent<entry>(1, word, ht[h].entries);

			/* add it to the front of the linked list */
			ht[h].entries = ep;
		}, rwlock);
	}



public:
	freq(buckets bht, pool_base &pool) : ht{bht}, pop{pool} {
	}

	/* break a test file into words and call count() on each one */
	void count_all_words(const char *fname)
	{
		FILE *fp;
		int c;
		char word[MAXWORD];
		char *ptr;

		if ((fp = fopen(fname, "r")) == NULL)
			throw std::runtime_error(std::string("fopen: ") + fname);

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

private:
	/* pointer to the buckets */
	buckets ht;
	pool_base &pop;
	static const int MAXWORD = 8192;
};

struct root {
	buckets ht;	/* word frequencies */
	/* ... other things we store in this pool go here... */
};

int main(int argc, char *argv[])
{
	int arg = 2;	/* index into argv[] for first file name */

	if (argc < 3) {
		std::cerr << "usage: " << argv[0]
			  << " pmemfile wordfiles..." << std::endl;
		exit(1);
	}

	auto pop = pool<root>::open(argv[1], LAYOUT);
	auto q = pop.get_root();

	/* before starting, see if buckets have been allocated */
	if (q->ht == nullptr) {
		transaction::exec_tx(pop, [&q]() {
			q->ht = make_persistent<bucket[NBUCKETS]>();
		});
	}

	int nfiles = argc - arg;
	std::vector<std::thread> threads;

	for (int i = 0; i < nfiles; ++i)
		threads.emplace_back(&freq::count_all_words, freq(q->ht, pop),
				argv[arg++]);

	for (auto &t : threads)
		t.join();

	pop.close();
	exit(0);
}
