## pmem programming tutorial

See the [slides](./slides.pdf) for the tutorial.  The rest of the
files in this repo provide a simple example to help illustrate how
a program is converted to use persistent memory.  There are four
programs here, the first two have nothing to do with persistent
memory, they just establish a simple example program that
needs conversion.  The second two programs show how to
use pmem from C and C++.


### The programs

#### `freq.c`
Simple C program for counting word frequency on a list on text files.
It uses a hash table with linked lists in each bucket.

#### `freq_mt.c`
Convert `freq.c` to be multi-threaded.  A separate thread is created
for each text file.

#### `freq_pmem.c`
Convert `freq_mt.c` to mode the hash table to persistent memory and use
libpmemobj for pmem allocation, locking, and transactions.

#### `freq_pmem_cpp.c`
Similar to `freq_pmem.c` but uses the C++ bindings to libpmemobj.

####  `freq_pmem_print.c`
C program to print the frequecy counts from the pmem-reseident hash table.


### Using the programs

```
$ git clone http://github.com/andyrudoff/pmemtutorial
Cloning into 'pmemtutorial'...
remote: Counting objects: 13, done.
remote: Compressing objects: 100% (9/9), done.
remote: Total 13 (delta 2), reused 13 (delta 2), pack-reused 0
Unpacking objects: 100% (13/13), done.
Checking connectivity... done.

$ cd pmemtutorial

$ make
cc -g -Wall -Werror -std=gnu99   -c -o freq.o freq.c
cc -o freq -g -Wall -Werror -std=gnu99 freq.o
cc -g -Wall -Werror -std=gnu99   -c -o freq_mt.o freq_mt.c
cc -o freq_mt -g -Wall -Werror -std=gnu99 freq_mt.o -pthread
cc -g -Wall -Werror -std=gnu99   -c -o freq_pmem.o freq_pmem.c
cc -o freq_pmem -g -Wall -Werror -std=gnu99 freq_pmem.o -lpmem -lpmemobj -pthread
cc -g -Wall -Werror -std=gnu99   -c -o freq_pmem_print.o freq_pmem_print.c
cc -o freq_pmem_print -g -Wall -Werror -std=gnu99 freq_pmem_print.o -lpmem -lpmemobj -pthread
g++ -g -Wall -Werror -std=gnu++11   -c -o freq_pmem_cpp.o freq_pmem_cpp.cpp
g++ -o freq_pmem_cpp -g -Wall -Werror -std=gnu99 freq_pmem_cpp.o -lpmem -lpmemobj -pthread

$ freq -p words.txt
1 is
1 all
1 for
2 to
1 men
1 good
2 the
1 come
1 their
1 Now
1 time
1 country
1 aid
1 of

$ freq_mt -p words.txt words.txt words.txt
3 is
3 all
3 for
6 to
3 men
3 good
6 the
3 come
3 their
3 Now
3 time
3 country
3 aid
3 of

$ pmempool create obj --layout=freq -s 1G freqcount

$ pmempool info freqcount
Part file:
path                     : freqcount
type                     : regular file
size                     : 1073741824

POOL Header:
Signature                : PMEMOBJ
Major                    : 3
Mandatory features       : 0x0
Not mandatory features   : 0x0
Forced RO                : 0x0
Pool set UUID            : 3bfdc703-69f8-4e0d-ad24-ba8888cd5859
UUID                     : 4973900d-787b-4c8c-abb9-80f51dcf3589
Previous part UUID       : 4973900d-787b-4c8c-abb9-80f51dcf3589
Next part UUID           : 4973900d-787b-4c8c-abb9-80f51dcf3589
Previous replica UUID    : 4973900d-787b-4c8c-abb9-80f51dcf3589
Next replica UUID        : 4973900d-787b-4c8c-abb9-80f51dcf3589
Creation Time            : Fri Jun 23 2017 07:31:18
Alignment Descriptor     : 0x000007f737777310[OK]
Class                    : ELF64
Data                     : 2's complement, little endian
Machine                  : AMD X86-64
Checksum                 : 0x395cdf2ab2411d91 [OK]

PMEM OBJ Header:
Layout                   : freq
Lanes offset             : 0x2000
Number of lanes          : 1024
Heap offset              : 0x302000
Heap size                : 1070587904
Checksum                 : 0x4b690400b1659666 [OK]
Root offset              : 0x0

$ freq_pmem_print freqcount

$ freq_pmem freqcount words.txt words.txt words.txt

$ freq_pmem_print freqcount
3 is
3 all
3 for
6 to
3 men
3 good
6 the
3 come
3 their
3 Now
3 time
3 country
3 aid
3 of

$ freq_pmem_cpp freqcount words.txt words.txt words.txt

$ freq_pmem_print freqcount
6 is
6 all
6 for
12 to
6 men
6 good
12 the
6 come
6 their
6 Now
6 time
6 country
6 aid
6 of
```
