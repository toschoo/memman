/* -----------------------------------------------------------------------
 * Buddy System (see Knuth, Vol. 1, Sec. 2.5)
 * ------------
 *
 *  (c) Tobias Schoofs, 2010 -- 2020
 *      This code is in the Public Domain.
 *
 * The Buddy System is a fast and memory-efficient
 * dynamic memory management system.
 *
 * Memory blocks are allocated as chunks whose size is
 * a power of two. There is some overhead in case the user
 * allocates large chunks that lie in between two powers of two.
 * But it is in the user's hand to opimise her code.
 * 
 * The smallest allocation size is fixed as MINSIZE.
 * Every memory block has therefore a size that is
 * a multiple of MINSIZE. 
 *
 * The available memory is split into two halves:
 * - the first half (which is a power of two) is used as main heap
 * - the second half contains the bookkeeping structures and
 *   an emergency heap used, when the main heap runs out of memory.
 *   The emergency heap uses the simpler (and slightly less efficient)
 *   First-Fit algorithm.
 *
 * Bookkeeping consists of two memory regions:
 * - the "avalable" area
 * - the "size" area
 *
 * The available area contains pointers to lists of available blocks.
 * There is one list per exponent, i.e. 2^3, 2^4, ... 2^max
 * where 2^max = size of the main heap and assuming that MINSIZE = 8.
 * 
 * The list of available blocks is stored in the main heap itself.
 * Available blocks are seen as MINSIZE blocks each part of a
 * doubly linked list. The next and previous pointers in each block
 * are 32bit pseudo pointers (only considering the heap).
 * The max size of the main heap is therefore 4 GiB.
 * This also implies that MINSIZE must be at leat 8 bytes.
 *
 * When a block of size n is allocated we search in the available
 * lists with index >= log2(n). If we don't find a block
 * of the requested size, we choose a greater size, split the blocks
 * and add them to the corresponding available lists
 * until we reach the requested size. Finally, when a block is
 * handed over to the user, it is removed from the available list
 * and the list data are cleared out from the block.
 *
 * We further remember the size of the block. This information
 * is stored in the size area. The size area stores the exponent
 * of the size. Since the maximum exponent is 32, 6 bits are
 * sufficient to store the size of one block.
 * The size of the size area can therefore be computed as
 *
 *  ((HeapSize / MINSIZE) * 6) / 8
 *
 * The following diagram shows the memory layout:
 *
 *     +---------------+--------+--+----+    
 *     |               |        |  |    |
 *     +---------------+--------+--+----+
 *     ^               ^        ^  ^
 *     |               |        |  |
 *     main heap      emergency |  size area
 *                    heap      |
 *                            available area
 *
 *
 * Remembering the size serves several purposes:
 * 1) we can quickly decide if a free request is valid or not
 *    (since we erase the size information on free)
 * 2) we can quickly find the corresponding available list
 * 3) we can quickly find the buddy of the block (i.e. the block
 *    aligned to this one in memory). If the buddy is free,
 *    we join the blocks repeatedly to optimise the number
 *    of large blocks available.
 *
 * -----------------------------------------------------------------------
 */
#include <buddy.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* -----------------------------------------------------------------------
 * MINSIZE: minimal allocation size
 * -----------------------------------------------------------------------
 */
#define MINSIZE     8

/* ------------------------------------------------------------------------
 * Some shortcuts
 * ------------------------------------------------------------------------
 */
#define FOUND    BUDDY_HEAP_FOUND
#define NOTFOUND BUDDY_HEAP_NOTFOUND
#define INTERNAL BUDDY_HEAP_INTERNAL
#define OK BUDDY_HEAP_OK

/* ------------------------------------------------------------------------
 * Check a result: does it mean "found something"?
 * ------------------------------------------------------------------------
 */
static inline char found(char rc) {
	return (rc >= 0 && (rc & NOTFOUND) != NOTFOUND);
}

/* ------------------------------------------------------------------------
 * Helpers:
 * - NOBLOCK is NULL for our pseudo block pointer type
 * - Dereference such a pointer
 * ------------------------------------------------------------------------
 */
#define NOBLOCK 0xffffffff
#define REFBLOCK(n) ((block_list_t*)block2ptr(h,n))

/* --------------------------------------------------------------------------
 * Convert pseudo block address (uint32_t) to real pointer
 * --------------------------------------------------------------------------
 */
static inline void *block2ptr(buddy_heap_t *h, uint32_t add) {
	return (add == NOBLOCK ? NULL :
                ((void*)((uintptr_t)(add) + (uintptr_t)h->mh)));
}

/* --------------------------------------------------------------------------
 * Convert real pointer into pseudo block address (uint32_t)
 * --------------------------------------------------------------------------
 */
static inline uint32_t ptr2block(buddy_heap_t *h, void *ptr) {
	return (ptr == NULL ? NOBLOCK :
	        (uint32_t)((uintptr_t)(ptr) - (uintptr_t)h->mh));
}

/* --------------------------------------------------------------------------
 * Convert pseudo block address to size address
 * --------------------------------------------------------------------------
 */
static inline uint32_t block2size(uint32_t add) {
	return (add>>3);
}

/* --------------------------------------------------------------------------
 * count leading zeros (clz)
 * --------------------------------------------------------------------------
 */
#ifdef __GNUC__
#define clz __builtin_clz
#endif

/* --------------------------------------------------------------------------
 * log2
 * based on the formula for 32bit integers:
 * log2 = 32 - (1 + clz)
 * --------------------------------------------------------------------------
 */
static inline uint8_t buddy_log2(uint32_t n) {
	return (8*sizeof(uint32_t) - 1 - clz(n));
}

/* --------------------------------------------------------------------------
 * modulus division by power of 2
 * --------------------------------------------------------------------------
 */
static inline uint32_t modpow2(uint32_t n, uint32_t d) {
	return (n & (d-1));
}

/* --------------------------------------------------------------------------
 * get next power of 2, i.e.:
 * if sz == 0: 1
 * if sz == 2^k for some k >= 0: sz
 * else: nextpow2(sz) = 2^k for the smallest k, such that 2^k > sz
 * 
 * Behold the implementation:
 * we set all bits below the msb to 1
 * then we add 1, e.g.:
 * 0101 (5)
 * 0111 (7)
 * 1000 (8)
 * --------------------------------------------------------------------------
 */
static inline uint32_t nextpow2(uint32_t sz) {
	sz--;
	sz |= sz >> 1;
	sz |= sz >> 2;
	sz |= sz >> 4;
	sz |= sz >> 8;
	sz |= sz >> 16;
	return (sz+1);
}

/* --------------------------------------------------------------------------
 * find the buddy for a given block address
 * buddy = block + 2^k if block = 0 (mod 2^(k+1)) and
 *         block - 2^k otherwise
 * where 2^k is the size of the block
 * --------------------------------------------------------------------------
 */
static inline uint32_t findbuddy(uint32_t block, uint8_t s) {
	uint32_t k = 1 << s;
	return ((block & ((k << 1) - 1)) == 0 ? block + k
	                                      : block - k);
}

/* --------------------------------------------------------------------------
 * Block size interface
 * --------------------------------------------------------------------------
 */
static inline void init_size(buddy_heap_t *h);
static inline void putsize(buddy_heap_t *h, int i, char c);
static inline void erasesize(buddy_heap_t *h, int i);
static inline char getsize(buddy_heap_t *h, int i);
static inline uint32_t block2size(uint32_t add);

/* --------------------------------------------------------------------------
 * Block list interface
 * --------------------------------------------------------------------------
 */
static inline void init_avail(buddy_heap_t *h);
static inline void block_insert(buddy_heap_t *h, uint32_t list,
                                                 uint32_t add);
static inline uint32_t block_remove(buddy_heap_t *h, uint32_t list,
                                                     uint32_t add);
static inline char block_is_in(buddy_heap_t *h, uint32_t list,
                                                uint32_t add);
static inline void block_clean(buddy_heap_t *h, uint32_t add);

/* --------------------------------------------------------------------------
 * "High level" interface
 * ----------------------
 * binsert: insert a block into the available list of size 2^sz
 * --------------------------------------------------------------------------
 */
static inline void binsert(buddy_heap_t *h, uint32_t add, uint8_t sz) {
	block_insert(h, h->ah[sz], add);
	h->ah[sz] = add; // we always insert at the head
	assert(h->ah[sz] < h->msize || h->ah[sz] == NOBLOCK);
}

/* --------------------------------------------------------------------------
 * bremove: remove a block from the available list of size 2^sz
 * --------------------------------------------------------------------------
 */
static inline void bremove(buddy_heap_t *h, uint32_t add, uint8_t sz) {
	h->ah[sz] = block_remove(h, h->ah[sz], add);
	assert(h->ah[sz] < h->msize || h->ah[sz] == NOBLOCK);
}

/* --------------------------------------------------------------------------
 * bisin: check if a block is in available list of size 2^sz
 * 1 if it is in
 * 0 if not
 * --------------------------------------------------------------------------
 */
static inline char bisin(buddy_heap_t *h, uint32_t add, uint8_t sz) {
	return block_is_in(h, h->ah[sz], add);	
}

/* --------------------------------------------------------------------------
 * bsplit: split a block in two:
 * remove it from its available list
 * insert the block and block + 2^(sz/2) into the next available list
 * --------------------------------------------------------------------------
 */
static inline void bsplit(buddy_heap_t *h, uint32_t add, uint8_t sz) {
	uint32_t s;
	bremove(h, add, sz); sz--; s = 1<<sz;
	binsert(h, add+s, sz);
	binsert(h, add, sz);
}

/* --------------------------------------------------------------------------
 * bjoin: repeatedly join a block with its buddy:
 * - find the buddy
 * - remove the body and the block (if it's not the first turn)
 * - compute the address of the joined block (= the lower address)
 * - insert the joined block in the next smaller available list
 * - repeat...
 * if the original block was joined, return 1 else return 0
 * --------------------------------------------------------------------------
 */
static inline char bjoin(buddy_heap_t *h, uint32_t add, uint8_t sz) {
	char rc = 0;
	uint32_t b = add;

	for(uint8_t s = sz; s < h->AMAX; s++) {
		uint32_t buddy = findbuddy(b,s);

		if (bisin(h, buddy, s)) {
			bremove(h, buddy, s);
			if (rc != 0) bremove(h, b, s);
			b = b < buddy ? b : buddy;
			binsert(h, b,s+1);
			if (rc == 0) rc = 1;
		} else break;
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * bextend: try to extend the current block
 *          parameters: block address,
 *                      current size,
 *                      intended size.
 *          returns 1 on success and 0 otherwise.
 *
 * - tries to extend the given block by adding buddies
 * - the buddies need to be at the "right" of the block
 *   (i.e. the address must be greater)
 * - there must be a buddy for each step up to size s/2
 * - we make a "dry run" to test the conditions and,
 *   if ok, we perform a real run, removing the buddies
 *   and changing the block size.
 * --------------------------------------------------------------------------
 */
static inline char bextend(buddy_heap_t *h, uint32_t b, uint8_t c, uint8_t s)
{
	char rc = 0;
	uint8_t i = c;

	// dry run
	for(; i<s; i++) {
		uint32_t buddy = findbuddy(b,i);
		if (buddy < b) break;
		if (!bisin(h,buddy,i)) break;
	}
	// real run
	if (i == s) {
		for(i=c; i<s; i++) {
			uint32_t buddy = findbuddy(b,i);
			bremove(h,buddy, i);
		}
		uint32_t k = block2size(b);
		erasesize(h,k);
		putsize(h,k,s);
		rc = 1;
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * bshrink: shrink a block to a given size
 *          parameters: block address,
 *                      current size,
 *                      intended size.
 *          The function always succeeds.
 *
 * - We first change the size
 * - Then we cut off the block with the intended size
 * - Then we proceed as follows until the remain is 0:
 *   - If the remainder is a power of two
 *     we insert the remainder and we are done.
 *   - Otherwise we split the the remainder into two parts:
 *     + the half of the next power of two
 *     + the difference of the remainder and that half                           
 *   - The half is inserted
 *   - The remainder is set to the second part
 *
 * This always works and never leaves a remainder < minsize
 * because all block sizes are multiples of minsize.
 * (All powers of 2 >= minsize are multiples of minsize.)
 * --------------------------------------------------------------------------
 */
static inline void bshrink(buddy_heap_t *h, uint32_t b, uint8_t c, uint8_t s)
{
	uint32_t cz = 1 << c;
	uint32_t sz = 1 << s;

	uint32_t k = block2size(b);
	erasesize(h,k); putsize(h,k,s);

	b += sz; binsert(h,b,s); b += sz;

	cz -= sz << 1;
	
	while (cz > 0) {
		k = nextpow2(cz);
		if (cz != k) k >>= 2;
		binsert(h, b, buddy_log2(k));
		cz -= k; b += k;
	}
}

/* --------------------------------------------------------------------------
 * malloc:
 * - find an available block starting with the exact size
 * - going up to AMAX
 * - if a block was found split it until we reach the exact size
 * - remove the block from the available list
 * - remember the size
 * --------------------------------------------------------------------------
 */
static uint32_t getblock(buddy_heap_t *h, uint32_t sz) {
	uint32_t b = NOBLOCK;
	uint8_t s = buddy_log2(sz);
	uint8_t i;

	// find available block, such that sz <= i <= AMAX
	for (i=s; i<=h->AMAX; i++) {
		if (h->ah[i] != NOBLOCK) {
			b = h->ah[i]; break;
		}
	}

	// get available block
	if (i <= h->AMAX) {
		for (;i>s;i--) {
			b = h->ah[i];
			if (b == NOBLOCK) break;
			bsplit(h, b, i);
		}
	}

	// printf("i: %u | b: %u\n", i, b);

	// remove block and remember size
	if (i == s && b != NOBLOCK) {
		assert(getsize(h, block2size(b)) == 0);
		bremove(h,b,s);
		putsize(h,block2size(b),s);
	} else b = NOBLOCK;

	return b;
}

/* --------------------------------------------------------------------------
 * free:
 * - if block is not a multiple of MINSIZE: it's not ours!
 * - find the size in the size area
 * - (if size = 0, this block was not allocated!)
 * - erase the size
 * - join block with its buddy (repeatedly)
 * - if it could not be joint,
 *   just insert it in available list of the exact size
 * --------------------------------------------------------------------------
 */
static int freeblock(buddy_heap_t *h, uint32_t block) {
	int rc = NOTFOUND;

	// check if block is multiple of minsize (otherwise error)
	if (modpow2(block,MINSIZE) == 0) {
		// check if block is in use (otherwise error)
		uint8_t s = getsize(h, block2size(block));
		if (s != 0) {
			// printf("size: %hhu\n", s);
			erasesize(h, block2size(block));
			if (!bjoin(h, block, s)) binsert(h,block,s);
			rc = OK;
		}
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * realloc:
 * - if b is not a multiple of MINSIZE or the size of b is not found:
 *   b was not allocated by us
 * - if size of b equals the requested size: we are done
 * - if requested size is greater:
 *   + if extend block works we are done
 *   + otherwise get a new block of the requested size
 *   + copy the content
 *   + free the original block
 * --------------------------------------------------------------------------
 */
static uint32_t extendblock(buddy_heap_t *h,
           uint32_t b, uint32_t sz, int *rc) {
	uint32_t ret = NOBLOCK;

	if (modpow2(b, MINSIZE) != 0) *rc = NOTFOUND; else {

		// find current size of block
		uint8_t cs = getsize(h, block2size(b));

		if (cs == 0) *rc = NOTFOUND; else {
			uint32_t csz = 1 << cs;

			if (csz == sz) ret = b;
			else if (csz < sz) {
				if (bextend(h, b, cs, buddy_log2(sz))) 
					ret = b;
				else {
					ret = getblock(h, sz);
					if (ret != NOBLOCK) {
						memcpy(block2ptr(h,ret),
						       block2ptr(h,b),csz);
						*rc = freeblock(h,b);
						assert(((*rc) & NOTFOUND) == 0);
						assert((*rc) >= 0);
						*rc = OK;
					}
				}
			} else if (csz > sz) {
				bshrink(h, b, cs, buddy_log2(sz));
				ret = b;
			}
		}
	}
	return ret;
}

/* --------------------------------------------------------------------------
 * printBlock: Print one block with colour
 * --------------------------------------------------------------------------
 */
static inline void printBlock(uint32_t add, uint32_t csz, char *prfx) {
	printf("%s%u\033[0m|", prfx, csz);
}

/* --------------------------------------------------------------------------
 * printUsed: Print a block in use
 * --------------------------------------------------------------------------
 */
static void printUsed(uint32_t add, uint32_t csz) {
	printBlock(add, csz, "\033[31m");
}

/* --------------------------------------------------------------------------
 * printFree: Print a free block
 * --------------------------------------------------------------------------
 */
static void printFree(uint32_t add, uint32_t csz) {
	printBlock(add, csz, "\033[32m");
}

/* --------------------------------------------------------------------------
 * Print all blocks and compute stats
 * ----------------------------------
 * The function computes the statistics:
 * - total memory (sum)
 * - memory used  (usd)
 * - free memory  (fre)
 * and, if p != 0, prints the size of each block in
 * - red, if it is used
 * - green, if it is available
 * --------------------------------------------------------------------------
 */
static void printBlocks(buddy_heap_t *h, char p, uint32_t *sum,
		                                 uint32_t *usd,
						 uint32_t *fre) {
	uint32_t block = 0;
	do {
		// if the block is used, we find its size in the size area
		uint8_t f = 1;
		uint8_t s = getsize(h, block2size(block));

		// otherwise we need to search it in the available area
		if (s == 0) {
			f = 0;
			for(; s<=h->AMAX; s++) {
				if (bisin(h, block, s)) break;
			}
			if (s > h->AMAX) {
				if (p) printf("LOST BLOCK: %u\n", block);
				break;
			}
		}

		// compute the effective size
		uint32_t sz = 1 << s;

		// print used or free
		if (f) {
			(*usd) += sz;
			if (p) printUsed(block, sz);
		} else {
			(*fre) += sz;
			if (p) printFree(block, sz);
		}
		(*sum)+=sz;
		block += sz; // next block
	} while (block < h->msize);
}

/* ------------------------------------------------------------------------
 * Public Interface
 * ------------------------------------------------------------------------
 * Init   : set non-constant global vars
 * --------
 * eh     : set to main heap + main heap size
 * AMAX   : log2 of msize (the entire heap)
 * asize  : size of all available lists
 *          from 0 - AMAX (note that are more than we need:
 *                         1, 2 and 4 are not used!)
 * ssize  : ssize is set to msize / 8 (8 is the miminal block size)
 *          multiplied by 6 (because each size block has 6 bits)
 *          divided by 8 (8 bits per byte)
 *          add one byte
 * esize  : size of the emergency heap is half of hs (=msize)
 *          - the size for book keeping (asize + ssize)
 * ah     : available area starts after emergency heap
 * sh     : size area starts after available area
 * ------------------------------------------------------------------------
 * - set all bytes in the the main heap to 0xff (NOBLOCK)
 *   this is initialises the lists in the blocks to empty
 * - init the size area
 * - init the avail area
 * ------------------------------------------------------------------------
 */
int buddy_init(buddy_heap_t *h) {
	if (h->mh != 0 && h->hs != 0) {
		int rc = OK;
		h->msize = h->hs / 2;
		h->eh = h->mh + h->msize;
		h->AMAX = buddy_log2(h->msize);
		h->asize = (h->AMAX+1)*sizeof(uint32_t);
		h->ssize = h->msize / 8 + 1;
		h->ssize *= 6;
		h->ssize /= 8;
		h->esize = h->msize - (h->asize + h->ssize);
		h->ah = (uint32_t*)((uintptr_t)h->eh + h->esize);
		h->sh = (uint8_t*)((uintptr_t)h->ah + h->asize);
		memset((void*)h->mh, 0xff, h->msize);
		printf("HEAP : %p\n", (void*)h->mh);
		printf("EHEAP: %p\n", (void*)h->eh);
		printf("AVAIL: %p\n", (void*)h->ah);
		printf("SIZE : %p\n", (void*)h->sh);
		printf("AMAX : %u\n", h->AMAX);
		printf("BOOK : %u%%\n", ((h->asize+h->ssize)*100)/h->msize);
		init_size(h);
		init_avail(h);
		if (h->e) {
			h->ffh.mh = h->eh;
			h->ffh.hs = h->esize;
			rc = ffit_init(&h->ffh);
		}
		return rc;
	}
	return -1;
}

/* --------------------------------------------------------------------------
 * malloc
 * --------------------------------------------------------------------------
 */
void *buddy_get_block(buddy_heap_t *h, size_t sz) {
	void *ret = NULL;
	if (sz > 0) {
		uint32_t s = sz < MINSIZE ? MINSIZE : nextpow2(sz);
		if (s < h->msize) {
			// lock
			uint32_t b = getblock(h, s);
			if (b != NOBLOCK) ret = block2ptr(h, b);
			else if (h->e) {
				ret = ffit_get_block(&h->ffh, sz);
			}
			// unlock
		}
	}
	return ret;
}

/* --------------------------------------------------------------------------
 * free
 * --------------------------------------------------------------------------
 */
int buddy_free_block(buddy_heap_t *h, void *ptr) {
	int rc = NOTFOUND;
	if ((uintptr_t)ptr < h->mh ||
	    (uintptr_t)ptr >= h->mh+h->msize+h->esize) {
		// error
	} else if ((uintptr_t)ptr >= h->eh) {
		if (h->e) {
			rc = ffit_free_block(&h->ffh, ptr);
		}
		// else error
	} else {
		// lock
		rc = freeblock(h,ptr2block(h,ptr));
		// unlock
		if ((rc & NOTFOUND) == NOTFOUND) rc = NOTFOUND;
		else if (rc < 0) rc = INTERNAL; else rc = OK;
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * realloc
 * --------------------------------------------------------------------------
 */
void *buddy_extend_block(buddy_heap_t *h,
           void *ptr, size_t sz, int *rc) {
	void *ret = NULL;

	// rc is only relevant for free, i.e. sz == 0
	*rc = OK;

	// if pointer is null: malloc
	if (ptr == NULL) ret = buddy_get_block(h,sz);

	// if size is 0 free
	else if (sz == 0) *rc = buddy_free_block(h,ptr);

	// check if pointer is valid
	else if ((uintptr_t)ptr >= h->mh+h->hs) {
		// error

	// check if pointer is in the main heap
	// and handle in the emergency heap otherwise
	} else if ((uintptr_t)ptr >= h->eh) {
		if (h->e) {
			ret = ffit_extend_block(&h->ffh, ptr, sz, rc);
		}
		// error 

	// handle in main heap
	} else {
		// compute effective size
		uint32_t s = sz < MINSIZE ? MINSIZE : nextpow2(sz);

		if (s < h->msize) {
			// lock
			ret = block2ptr(h,extendblock(h,
			                  ptr2block(h,ptr),s,rc));
			// unlock
		}
	}
	return ret;
} 

/* --------------------------------------------------------------------------
 * print (debug)
 * --------------------------------------------------------------------------
 */
void buddy_print_heap(buddy_heap_t *h) {
	uint32_t mem = 0;
	uint32_t usd = 0;
	uint32_t fre = 0;

	printBlocks(h, 1, &mem, &usd, &fre);
	printf("\nTotal    : %09u\n", mem);
	printf("\033[31mUsed     : %09u\033[0m", usd);
	printf("\033[31m (%u%%)\033[0m", (100*usd)/mem);
	printf("\n");
	printf("\033[32mFree     : %09u\033[0m\n", fre);
	if (fre + usd != mem) {
		printf("\033[31mmissing: %09u\033[0m\n",
		                       mem - (usd+fre));
	}
	if (h->e) {
		printf("### EMERGENCY ##############\n");
		ffit_print_heap(&h->ffh);
	}
}

/* --------------------------------------------------------------------------
 * Get statistics (we support only mem, used and free,
 *                 there is no watermark and no steps)
 * --------------------------------------------------------------------------
 */
void  buddy_get_stats(buddy_heap_t *h,
		      uint32_t *mem,  // heap size
                      uint32_t *usd,  // used memory 
                      uint32_t *fre)  // average search steps
{

	printBlocks(h, 0, mem, usd, fre);
}

/* --------------------------------------------------------------------------
 * Block size interface
 * init size: set all bytes in the size area to 0
 * --------------------------------------------------------------------------
 */
static inline void init_size(buddy_heap_t *h) {
	memset(h->sh, 0, h->ssize);
}

/* --------------------------------------------------------------------------
 * pusize:
 * p is the index * 6, because each block has 6 bits
 * y is the corresponding byte
 * b is the bit in byte y
 * --------------------------------------------------------------------------
 */
static inline void putsize(buddy_heap_t *h, int i, char c) {
	uint32_t p = i*6;
	uint32_t y = p/8;
	uint32_t b = modpow2(p,8);
	h->sh[y] |= ((c<<2) >> b);
	h->sh[y+1] |= ((c<<2) << (8-b));
}

/* --------------------------------------------------------------------------
 * getsize:
 * p is the index * 6, because each block has 6 bits
 * y is the corresponding byte
 * b is the bit in byte y
 * --------------------------------------------------------------------------
 */
static inline char getsize(buddy_heap_t *h, int i) {
	uint32_t p = i*6;
	uint32_t y = p/8;
	uint32_t b = modpow2(p,8);
	uint8_t x = (h->sh[y] << b);
	x |= (h->sh[y+1] >> (8-b));
	return (x>>2);
}

/* --------------------------------------------------------------------------
 * erasesize:
 * p is the index * 6, because each block has 6 bits
 * y is the corresponding byte
 * b is the bit in byte y
 * --------------------------------------------------------------------------
 */
static inline void erasesize(buddy_heap_t *h, int i) {
	uint32_t p = i*6;
	uint32_t y = p/8;
	uint32_t b = modpow2(p,8);
	if (b == 0) {
	   h->sh[y] &= 0xff>>6;
	} else {
	   h->sh[y] &= 0xff<<(8-b);
	   h->sh[y+1] &= 0xff>>(b-2);
	}
}

/* --------------------------------------------------------------------------
 * Block list interface
 * --------------------------------------------------------------------------
 */
typedef struct {
	uint32_t nxt;
	uint32_t prv;
} block_list_t;

/* ------------------------------------------------------------------------
 * init available lists:
 * - set all bytes in the available area to 0xff
 *   this sets each block to NOBLOCK (0xffffffff)
 * - insert address 0 into the available list for msize (entire heap)
 * ------------------------------------------------------------------------
 */
static inline void init_avail(buddy_heap_t *h) {
	memset((void*)h->ah, 0xff, h->asize);
	binsert(h, 0, buddy_log2(h->msize));
}

/* ------------------------------------------------------------------------
 * insert a block
 * ------------------------------------------------------------------------
 */
static inline void block_insert(buddy_heap_t *h, uint32_t list,
                                                 uint32_t add) {
	block_list_t *tmp = block2ptr(h, add);
	tmp->nxt = list;
	tmp->prv = NOBLOCK;
	if (list != NOBLOCK) {
		tmp = block2ptr(h, list);
		tmp->prv = add;
	}
}

/* ------------------------------------------------------------------------
 * remove a block
 * ------------------------------------------------------------------------
 */
static inline uint32_t block_remove(buddy_heap_t *h, uint32_t list,
                                                     uint32_t add) {
	block_list_t *tmp = block2ptr(h, list);
	uint32_t head = list;
	if (list == add) {
		if (list != NOBLOCK) {
			head = tmp->nxt;
			block_clean(h, list);
		}
	} else {
		while(tmp != NULL && tmp->nxt != add) {
			tmp = block2ptr(h, tmp->nxt);
		}
		if (tmp != NULL) {
			block_list_t *node = block2ptr(h, tmp->nxt);
			tmp->nxt = node->nxt;
			if (node->nxt != NOBLOCK) {
				REFBLOCK(node->nxt)->prv = node->prv;
			}
			block_clean(h, ptr2block(h, node));
		}
	}
	return head;
}

/* ------------------------------------------------------------------------
 * check if block 'add' is in list
 * ------------------------------------------------------------------------
 */
static inline char block_is_in(buddy_heap_t *h, uint32_t list, uint32_t add)
{
	char rc = 0;
	block_list_t *tmp = block2ptr(h, list);
	if (list == add) rc = 1; else {
		while(tmp != NULL && tmp->nxt != add) {
			tmp = block2ptr(h, tmp->nxt);
		}
		if (tmp != NULL) rc = 1;
	}
	return rc;
}

/* ------------------------------------------------------------------------
 * Clean a block (set the list bytes to NOBLOCK)
 * ------------------------------------------------------------------------
 */
static inline void block_clean(buddy_heap_t *h, uint32_t add) {
	memset(block2ptr(h, add), 0xff, 8); // if security: erase all!
}
