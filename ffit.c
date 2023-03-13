/* -----------------------------------------------------------------------
 * First Fit Method (see Knuth, Vol. 1, Sec. 2.5)
 * ----------------
 *
 * (c) Tobias Schoofs, 2010 -- 2020
 *     This code is in the Public Domain.
 *
 * The First Fit Method is a fast and memory-efficient
 * dynamic memory management system. It is especially
 * well-suited for allocation of larger blocks of memory.
 * Since it uses some space at the beginning and the end
 * of each memory block, a constant amount is overhead,
 * namely 5 bytes. When the heap is mainly used for
 * for large memory blocks, this overhead is negligible.
 *
 * The First Fit Method is much simpler than the buddy system.
 * But note that any overwriting of memory by user code will
 * effectively currupt the memory manager rendering it
 * unusuable. Our implementation of the the buddy system is
 * more robust. Overwriting will, in most cases, not cause
 * any corruption of library data.
 *
 * Furthermore, the buddy system is more resilient against
 * towards fragmentation. However, Knuth proposes some
 * optimisations to improve on this weakness.
 *
 * The ffit implementation here does not use extra memory
 * on the heap. Everything is implemented in 
 * - the struct ffit_heap_t and
 * - the memory blocks themselves.
 *
 * To refer to memory blocks internally a 4-byte pointer type
 * is used; this limits the heap size to 4 GiB. Furthermore,
 * the size of each block is stored as a 31-bit unsigned integer
 * (see below). This limits the greatest allocation possible to
 * 2 GiB. 
 *
 * The main component is the available list (avail), a doubly linked
 * list that contains all available blocks in the order of their size.
 * The allocator will grab the first block which is equal to or greater
 * than the requested size (therefore: first fit).
 *
 * The size of each block is stored as a 31-bit integer in the first
 * four bytes of that block. The 32-nd bit and the last byte of the
 * block are used to store a 1-bit tag: if the block is currently
 * used the tag is 1; otherwise it is 0. When a block is freed
 * it is merged with its neighbours if the corresponding tag is 0;
 * note that <block address>-1 and <block address>+<block size>
 * refer to the tag corresponding to the preceding and following
 * neighbour respectively. The merged neighbours are removed from
 * the available list and the resulting block (which, if both neighbours
 * are in use, is just the original block) is inserted in the list
 * respecting the order by block size.
 *
 * Here is a sketch of the block layout:
 * 
 *   +----+----+----+-------------+-+
 *   |    |    |    | ...         | |
 *   +----+----+----+-------------+-+
 *   ^    ^    ^    ^             ^
 *   |    |    |    |             |_ tag
 *   |    |    |    |
 *   |    |    |    |_ n bytes
 *   |    |    |   
 *   |    |    |_ previous pointer (4 bytes)
 *   |    |
 *   |    |_ next pointer (4 bytes); here starts user memory
 *   |
 *   |_ size and tag (4 bytes)
 *
 * Note that the next and previous pointers are needed only
 * when the block is available; when the block is used, these 8 bytes
 * are available for application use.
 *
 * The data structure implemented in the block (size, pointers, tag)
 * requires at least 4 + 2 x 4 + 1 = 13 byte. The minimal allocation
 * size is chosen as 32 byte. This also reflects the fact that
 * for each allocation, 5 bytes are wasted. For a 32-byte block,
 * the overhead is ~15%. 
 * -----------------------------------------------------------------------
 */

#include <ffit.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* -----------------------------------------------------------------------
 * MINSIZE: minimal allocation size
 * -----------------------------------------------------------------------
 */
#define MINSIZE 32

/* ------------------------------------------------------------------------
 * Helpers:
 * - NOBLOCK is NULL for our pseudo block pointer type
 * - Dereference such a pointer
 * ------------------------------------------------------------------------
 */
#define NOBLOCK 0xffffffff
#define REFBLOCK(n) ((block_t*)block2ptr(h->mh, n))

/* --------------------------------------------------------------------------
 * Convert pseudo block address (uint32_t) to real pointer
 * --------------------------------------------------------------------------
 */
static inline void *block2ptr(uintptr_t mh, uint32_t add) {
	return (add == NOBLOCK ? NULL :
                ((void*)((uintptr_t)(add) + (uintptr_t)mh)));
}

/* --------------------------------------------------------------------------
 * Convert real pointer into pseudo block address (uint32_t)
 * --------------------------------------------------------------------------
 */
static inline uint32_t ptr2block(uintptr_t mh, void *ptr) {
	return (ptr == NULL ? NOBLOCK :
	        (uint32_t)((uintptr_t)(ptr) - (uintptr_t)mh));
}

#define NOTFOUND 4

/* --------------------------------------------------------------------------
 * Get size 
 * --------------------------------------------------------------------------
 */
static inline uint32_t getsize(uint32_t sz) {
	return (sz>>1);
}

/* --------------------------------------------------------------------------
 * Set size 
 * --------------------------------------------------------------------------
 */
static inline uint32_t setsize(uint32_t sz) {
	return (sz<<1);
}

/* --------------------------------------------------------------------------
 * Get tag
 * --------------------------------------------------------------------------
 */
static inline uint8_t gettag(uint32_t sz) {
	return (uint8_t)(sz & 1);
}

/* --------------------------------------------------------------------------
 * Block list interface
 * --------------------
 * Note that the size also stores the tag bit!
 * --------------------------------------------------------------------------
 */
typedef struct {
	uint32_t sze;
	uint32_t nxt;
	uint32_t prv;
} block_t;

/* --------------------------------------------------------------------------
 * Helpers for the lazy
 * --------------------------------------------------------------------------
 */
typedef ffit_heap_t heap_t;

#define P2B(n) \
	ptr2block(h->mh, n)

#define B2P(n) \
	block2ptr(h->mh, n)

/* --------------------------------------------------------------------------
 * Tag block
 * --------------------------------------------------------------------------
 */
static inline void tag(block_t *b) {
        uint32_t s = b->sze >> 1;
	b->sze |= (uint32_t)1;
        *(((char*)b)+s-1) = 1;
}

/* --------------------------------------------------------------------------
 * Untag block
 * --------------------------------------------------------------------------
 */
static inline void untag(block_t *b) {
        uint32_t s = b->sze >> 1;
	if ((b->sze & (uint32_t)1) == 1)
		b->sze = 0xffffffff & (b->sze^(uint32_t)1);
        *(((char*)b)+s-1) = 0;
}

/* --------------------------------------------------------------------------
 * Check if byte is tagged
 * --------------------------------------------------------------------------
 */
static inline char tagged(uint8_t *w) {
	return (((*w) && (uint8_t)1) == 1);
}

/* --------------------------------------------------------------------------
 * Remove from available list
 * --------------------------------------------------------------------------
 */
static void bremove(heap_t *h, block_t *p) {
	if (p->prv != NOBLOCK)
		REFBLOCK(p->prv)->nxt = p->nxt;
	else h->first = p->nxt;
	
	if (p->nxt != NOBLOCK)
		REFBLOCK(p->nxt)->prv = p->prv;
	else h->last = p->prv;
}

/* --------------------------------------------------------------------------
 * Insert q before p in available list
 * --------------------------------------------------------------------------
 */
static void binsert(heap_t *h, block_t *p, block_t *q) {
	if (p->prv != NOBLOCK)
		REFBLOCK(p->prv)->nxt = P2B(q);
	else h->first = P2B(q);
	q->prv = p->prv;
	p->prv = P2B(q);
	q->nxt = P2B(p);
}

/* --------------------------------------------------------------------------
 * Find block immediately before address "end" in available list
 * --------------------------------------------------------------------------
 */
static block_t *bfind(heap_t *h, uint32_t end) {
	block_t *b = B2P(h->first);
	uint32_t a = h->first;
	char found = 0;

	for(;;) {
		if (b == NULL) break;
		uint32_t s = getsize(b->sze);
		if (a+s == end) {
			found=1; break;
		}
		a = b->nxt;
		b = B2P(a);
	}
	if (!found) b = NULL;
	return b;
}

/* --------------------------------------------------------------------------
 * Insert b into available list maintaining order by size
 * --------------------------------------------------------------------------
 */
static void binssort(heap_t *h, block_t *b) {
	if (h->first == NOBLOCK) {
		h->first = P2B(b);
		h->last =  h->first;
		b->nxt = NOBLOCK;
		b->prv = NOBLOCK;
	} else {
		block_t *p = B2P(h->first);
		uint32_t s = getsize(b->sze);
		for(;;) {
			uint32_t sz = getsize(p->sze);
			if (sz >= s) {
				binsert(h, p, b); break;
			}
			if (p->nxt == NOBLOCK) {
				p->nxt = P2B(b);
				b->prv = P2B(p);
				b->nxt = NOBLOCK;
				h->last = p->nxt;
				break;
			}
			p = B2P(p->nxt);
		}
	}
}

/* --------------------------------------------------------------------------
 * Replace p by q in available list
 * --------------------------------------------------------------------------
 */
static void breplace(heap_t *h, block_t *p, block_t *q) {
	bremove(h, p);
	binssort(h, q);
}

/* --------------------------------------------------------------------------
 * Find first block with at least "sz" in available list
 * --------------------------------------------------------------------------
 */
static uint32_t getblock(heap_t *h, uint32_t sz) {
	uint32_t b = NOBLOCK;
	block_t *p = B2P(h->first);
	while (p != NULL) {
		uint32_t s = getsize(p->sze);
		if (s >= sz) {
			// append new block
			if (s > sz + MINSIZE) {
				block_t *q = block2ptr(h->mh,
				             ptr2block(h->mh, p)+sz);
				q->sze = setsize(s-sz);
				untag(q);
				p->sze = setsize(sz);
				breplace(h,p,q);
			// remove
			} else {
				p->sze = setsize(getsize(p->sze));
				bremove(h,p);
			}
			tag(p); b = P2B(p); break;
		}
		p = B2P(p->nxt);
	}
	return b;
}

/* --------------------------------------------------------------------------
 * Add memory block at "add" into available list
 * --------------------------------------------------------------------------
 */
static int freeblock(heap_t *h, uint32_t add) {
	int rc = 0;
	block_t *b = block2ptr(h->mh, add);
	uint32_t s = getsize(b->sze);
	uint8_t  t = gettag(b->sze);

	if (!t) {rc = NOTFOUND;} else {


		// the block immediately before 'add'
		if (add > 0 && !tagged((uint8_t*)(block2ptr(h->mh, add-1)))) {
			block_t *p = bfind(h, add);
			// merge with previous
			if (p == NULL) {rc = -1;} else {
				p->sze = setsize(getsize(p->sze) + s);
				bremove(h,p);
				b = p;
			}
        	}
		// the block immediately following 'add'
		if (rc == 0) {
			block_t *q = block2ptr(h->mh, add+s);
			// merge with next
			if (add+s < h->hs && !gettag(q->sze)) {
				uint32_t ns = getsize(q->sze);
				b->sze = setsize(getsize(b->sze) + ns);
				bremove(h,q);
			} 

			// remove tag and insert
			untag(b); binssort(h,b);
		}
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * External interface: init heap 
 * --------------------------------------------------------------------------
 */
int ffit_init(ffit_heap_t *h) {
	int rc = -1;
	h->first = 0;
	h->last  = 0;
	block_t *b = (block_t*)h->mh;
	if (h->hs > 32) {
		rc = 0;
        	b->sze = setsize((uint32_t)h->hs);
                untag(b);
		b->nxt = NOBLOCK;
		b->prv = NOBLOCK;
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * External interface: get block of size 'sz' (a.k.a. malloc)
 * --------------------------------------------------------------------------
 */
void *ffit_get_block(ffit_heap_t *h, size_t sz) {
	void *ret = NULL;
	if (sz > 0) {
		// compute size: + 5 at least MINSIZE
		uint32_t s = (uint32_t)sz+5;
		if (s < MINSIZE) s = MINSIZE;
		if (s < h->hs) {
			// lock
			uint32_t b = getblock(h,s);
			if (b != NOBLOCK) ret = B2P(b+4);
			// unlock
		}
	}
	return ret;
}

/* --------------------------------------------------------------------------
 * External interface: free block identified by 'ptr' (a.k.a. as free)
 * --------------------------------------------------------------------------
 */
int ffit_free_block(ffit_heap_t *h, void *ptr) {
	int rc = 0;
	if ((uintptr_t)(ptr-4) >= h->mh &&
            (uintptr_t)(ptr+5) < h->mh + h->hs) {
		uint32_t b = P2B(ptr-4);
		// lock
		rc = freeblock(h, b);
		// lock
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * External interface: extend block (a.k.a. realloc)
 * --------------------------------------------------------------------------
 */
void *ffit_extend_block(ffit_heap_t *h, void *ptr, size_t sz, int *rc) {
	void *ret = NULL;

	// rc is only relevant for free, i.e. sz == 0
	*rc = 0;

	// if pointer is null: malloc
	if (ptr == NULL) ret = ffit_get_block(h,sz);

	// if size is 0 free
	else if (sz == 0) *rc = ffit_free_block(h,ptr);

	// check if pointer is valid
	else if ((uintptr_t)ptr >= h->mh+h->hs) {
		// error

	// handle in heap
	} else {
		// compute size: + 5 at least MINSIZE
		uint32_t s = (uint32_t)sz+5;
		if (s < MINSIZE) s = MINSIZE;
		if (s < h->hs) {
			uint32_t add = P2B(ptr-4);
			block_t *b = B2P(add);
			uint32_t os = getsize(b->sze);
			if (os == s) ret = ptr; else {
				ret = ffit_get_block(h,s);
				if (ret != NULL) {
					uint32_t k = os > s ? sz : os-5;
					memcpy((char*)ret+4, (char*)ptr+4, k);
					*rc = ffit_free_block(h,ptr);
				}
			
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
 * printheap: print all blocks in the heap
 * --------------------------------------------------------------------------
 */
void printheap(ffit_heap_t *h, char p, uint32_t *usd, uint32_t *fre) {
	block_t *b = (block_t*)h->mh;
	uint32_t add = 0;

	while (add < h->hs) {
		uint32_t s = getsize(b->sze);
		uint8_t  t = gettag(b->sze);
		if (t) {
			if (p) printUsed(add, s); 
			(*usd)+=s;
		} else {
			if (p) printFree(add, s); 
			(*fre)+=s;
		}
		add += s;
		b = B2P(add);
	}
}

/* --------------------------------------------------------------------------
 * External interface: print heap (debugging)
 * --------------------------------------------------------------------------
 */
void  ffit_print_heap(ffit_heap_t *h) {
	uint32_t usd = 0, fre = 0;
	uint32_t mem = (uint32_t)h->hs;
	printheap(h, 1, &usd, &fre);
	printf("\nTotal    : %09u\n", mem);
	printf("\033[31mUsed     : %09u\033[0m", usd);
	printf("\033[31m (%u%%)\033[0m", (100*usd)/mem);
	printf("\n");
	printf("\033[32mFree     : %09u\033[0m\n", fre);
	if (fre + usd != mem) {
		printf("\033[31mmissing: %09u\033[0m\n",
		                       mem - (usd+fre));
	}

}

/* --------------------------------------------------------------------------
 * External interface: get stats (debugging and benchmarking)
 * --------------------------------------------------------------------------
 */
void  ffit_get_stats(ffit_heap_t *h,
                     uint32_t *mem,   // heap size 
                     uint32_t *usd,   // used memory 
                     uint32_t *fre)   // available memory
{
        *mem   = (uint32_t)h->hs;
	printheap(h, 0, usd, fre);
}
