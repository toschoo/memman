/* -----------------------------------------------------------------------
 * Basis tests for Dynamic Memory Management Systems
 * -----------------------------------------------------------
 *
 * (c) Tobias Schoofs, 2010 -- 2020
 *     This code is in the Public Domain.
 * -----------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef USEKFFIT
char _fheap[1048576];
#include <ffit.h>
ffit_heap_t h;

#define heapinit() \
	h.mh = (uintptr_t)_fheap; \
	h.hs = 1048576; \
	rc = ffit_init(&h)

#define OK FFIT_HEAP_OK
#define getblock(n) ffit_get_block(&h,n)
#define freeblock(n) ffit_free_block(&h,n)
#define exblock(n,s,r) ffit_extend_block(&h,n,s,r)

#else
char _bheap[2097152];
#include <buddy.h>
buddy_heap_t h;
#ifdef WITH_EMERGENCY
#define E 1
#define H 1048576
#else
#define E 0
#define H 2097152
#endif
#define heapinit() \
	h.mh = (uintptr_t)_bheap; \
	h.hs = H; \
	h.e  = E; \
	rc = buddy_init(&h)
#define OK BUDDY_HEAP_OK
#define getblock(n) buddy_get_block(&h,n)
#define freeblock(n) buddy_free_block(&h,n)
#define exblock(n,s,r) buddy_extend_block(&h,n,s,r)
#endif

#define MINALLOC 8
#define MAXALLOC 8192
#define ITERS 750
#define PTRS 1000

typedef struct {
	char *ptr;
	size_t sz;
} pointer_t;

pointer_t ps[PTRS];

char *testheap;

int allocs = 0;
int frees  = 0;
int ifrees  = 0;
int reallocs = 0;
int preservd = 0;
int invalid = 0;

char testbuf[MAXALLOC];

/* ------------------------------------------------------------------------
 * Helper: Next Power of 2
 * ------------------------------------------------------------------------
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

/* ------------------------------------------------------------------------
 * Helper: Get random block size
 * ------------------------------------------------------------------------
 */
size_t randomBlockSize() {
	size_t s;
	uint16_t b = rand()%10;
	switch(b) {
		case 0: s = rand()%MAXALLOC; break;
		case 1: s = rand()%1024; break;
		case 2: s = rand()%512; break;
		case 3: s = rand()%256; break;
		case 4: s = rand()%128; break;
		default: s = rand()%64;
	}
	if (s == 0) s++;
	return s;
}

/* ------------------------------------------------------------------------
 * Helper: Get address distinct from ptr
 * ------------------------------------------------------------------------
 */
static inline void *nextaddress(void *ptr) {
	uint32_t b = (uint32_t)((char*)ptr - testheap);
	// fprintf(stderr, "%p - %p = %u\n", ptr, testheap, b);
	uint32_t n;
	for(n=b;n<b+MINALLOC;) {
		n += (uint32_t)randomBlockSize();
		n = nextpow2(n);
		// printf("n: %u (%u)\n", n, b);
	}
	return (void*)(testheap+n);
}

/* ------------------------------------------------------------------------
 * Helper: Allocate and store pointer of size s
 * ------------------------------------------------------------------------
 */
void *allocptr(size_t s) {
	void *ptr = getblock(s);
	if (ptr == NULL) {
		fprintf(stderr, "cannot allocate %zu bytes\n", s);
		return NULL;
	}
	int i = 0;
	for(;i<PTRS;i++) {
		if (ps[i].ptr == NULL) {
			ps[i].ptr = ptr;
			ps[i].sz  = s;
			allocs++;
			break;
		}
	}
	if (i == PTRS) {
		fprintf(stderr, "FULL!\n");
		freeblock(ptr);
		return NULL;
	}
	return ptr;
}

/* ------------------------------------------------------------------------
 * Helper: Free random pointer
 * ------------------------------------------------------------------------
 */
int randomfree() {
	for(int i=0;i<100;i++) {
		int p = rand()%PTRS;
		if (ps[p].ptr != NULL) {
			if (freeblock(ps[p].ptr) != OK) {
				fprintf(stderr, "cannot free pointer %p\n", ps[p].ptr);
				return -1;
			}
			ps[p].ptr = NULL;
			ps[p].sz = 0;
			frees++;
			break;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------
 * Helper: Validate pointers
 * - no address is used more than once
 * - no overlapping addresses
 * ------------------------------------------------------------------------
 */
int validateptrs() {
	for(int i=0;i<PTRS;i++) {
		if (ps[i].ptr != NULL) {
			for(int j=i+1;j<PTRS;j++) {
				if (ps[j].ptr == ps[i].ptr) {
					fprintf(stderr, "pointer %p used twice!\n", ps[i].ptr);
					return -1;
				}
				if (ps[i].ptr + ps[i].sz > ps[j].ptr &&
				    ps[i].ptr < ps[j].ptr + ps[j].sz) {
					fprintf(stderr, "%p + %zu  overlaps %p + %zu\n",
					                            ps[i].ptr, ps[i].sz,
					                            ps[j].ptr, ps[j].sz);
					return -1;
				}

			}
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------
 * Helper: Release all pointers
 * ------------------------------------------------------------------------
 */
int cleanptrs() {
	for(int i=0; i<PTRS; i++) {
		if (ps[i].ptr != NULL) {
			if (freeblock(ps[i].ptr) != OK) {
				fprintf(stderr, "cannot free pointer %p\n", ps[i].ptr);
				return -1;
			}
			ps[i].ptr = NULL;
			ps[i].sz = 0;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------
 * Test: Allocate and free
 * ------------------------------------------------------------------------
 */
int testSimpleAlloc() {
	void *ptr = NULL;
	
	size_t s = randomBlockSize();
	// fprintf(stderr, "allocating %zu bytes\n", s);
	ptr = getblock(s);
	if (ptr == NULL) {
		fprintf(stderr, "cannot allocate %zu bytes\n", s);
		return -1;
	}
	allocs++;
	int rc = freeblock(ptr);
	if (rc != OK) {
		fprintf(stderr, "cannot free pointer %p\n", ptr);
		return -1;
	}
	frees++;
	return 0;
}

/* ------------------------------------------------------------------------
 * Test: Realloc and free
 * ------------------------------------------------------------------------
 */
int testSimpleRealloc() {
	void *ptr = NULL;
	int rc;
	
	size_t s = randomBlockSize();
	// fprintf(stderr, "allocating %zu bytes\n", s);
	ptr = getblock(s);
	if (ptr == NULL) {
		fprintf(stderr, "cannot allocate %zu bytes\n", s);
		return -1;
	}
	allocs++;

	memcpy(testbuf, ptr, s);

	size_t n = randomBlockSize();
	ptr = exblock(ptr, s, &rc);
	if (ptr == NULL) {
		fprintf(stderr, "cannot reallocate %zu -> %zu bytes\n", s, n);
		return -1;
	}
	if (rc != OK) {
		fprintf(stderr, "cannot reallocate rc: %d\n", rc);
		return -1;
	}
	reallocs++;
	if (memcmp(testbuf, ptr, n > s ? s : n) != 0) {
		fprintf(stderr, "content not preserved\n");
		return -1;
	}
	rc = freeblock(ptr);
	if (rc != OK) {
		fprintf(stderr, "cannot free pointer %p\n", ptr);
		return -1;
	}
	frees++;
	return 0;
}

/* ------------------------------------------------------------------------
 * Test: Realloc as free
 * ------------------------------------------------------------------------
 */
int testFreeByRealloc() {
	void *ptr = NULL;
	int rc;
	
	size_t s = randomBlockSize();
	// fprintf(stderr, "allocating %zu bytes\n", s);
	ptr = getblock(s);
	if (ptr == NULL) {
		fprintf(stderr, "cannot allocate %zu bytes\n", s);
		return -1;
	}
	allocs++;

	ptr = exblock(ptr, 0, &rc);
	if (ptr != NULL) {
		fprintf(stderr, "cannot free using reallocate\n");
		return -1;
	}
	if (rc != OK) {
		fprintf(stderr, "cannot reallocate rc: %d\n", rc);
		return -1;
	}
	frees++;
	return 0;
}

/* ------------------------------------------------------------------------
 * Test: Realloc as Alloc
 * ------------------------------------------------------------------------
 */
int testAllocByRealloc() {
	void *ptr = NULL;
	int rc;
	
	size_t s = randomBlockSize();
	// fprintf(stderr, "allocating %zu bytes\n", s);

	ptr = exblock(NULL, s, &rc);
	if (ptr == NULL) {
		fprintf(stderr, "cannot allocate %zu using reallocate\n", s);
		return -1;
	}
	if (rc != OK) {
		fprintf(stderr, "cannot reallocate rc: %d\n", rc);
		return -1;
	}
	allocs++;
	rc = freeblock(ptr);
	if (rc != OK) {
		fprintf(stderr, "cannot free pointer %p\n", ptr);
		return -1;
	}
	frees++;
	return 0;
}

/* ------------------------------------------------------------------------
 * Test: Allocate up to 'a' pointers and free up to 'f'
 *       in random order 
 * ------------------------------------------------------------------------
 */
int testNAllocs(int a, int f) {
	int rc = 0;
	int n = a + f - 1; a--;

	size_t s = randomBlockSize();
	if (allocptr(s) == NULL) return -1;
	for(int i=0;i<n;i++) {
		char x = a == 0 ? 1 : rand()%2;
		x = f == 0 ? 0 : x;
		if (x) {
			rc = randomfree(); f--;
		} else {
 			s = randomBlockSize();
			if (allocptr(s) == NULL) rc = -1; else rc = 0;
			a--;
		}
		if (rc != 0) break;
	}
	rc = rc | cleanptrs();
	return rc;
}

/* ------------------------------------------------------------------------
 * Test: Allocate pointer try to free invalid pointer, free correct pointer
 *       Note: The buddy system recognises invalid pointers;
 *             the ffit method does not!
 * ------------------------------------------------------------------------
 */
int testAllocWrongFree() {
#ifndef NOFREEPROTECT
	void *ptr = NULL;
	void *wptr = NULL;
	
	size_t s = randomBlockSize();
	// fprintf(stderr, "allocating %zu bytes\n", s);
	ptr = getblock(s);
	if (ptr == NULL) {
		fprintf(stderr, "cannot allocate %zu bytes\n", s);
		return -1;
	}
	allocs++;

	// behaviour of free undefined if ptr does not exst!
	// this is not an error (according to libc)
	wptr = nextaddress(ptr);

	// fprintf(stderr, "freeing invalid pointer %p\n", wptr);
	int rc = freeblock(wptr);
	if (rc == OK) {
		fprintf(stderr, "freed invalid pointer %p != %p of size %u\n", ptr, wptr, nextpow2(s));
		invalid++;
	}

	rc = freeblock(ptr);
	if (rc != OK) {
		fprintf(stderr, "cannot free pointer %p\n", ptr);
		return -1;
	}
	frees++;
	ifrees++;
#endif
	return 0;
}

int main() {
	int rc = 0;
	memset(ps, 0, PTRS*sizeof(pointer_t));
	heapinit();
	if (rc != 0) {
		fprintf(stderr, "FAILED: cannot init heap\n");
		return -1;
	}
	testheap = getblock(MINALLOC);
	if (testheap == NULL) {
		fprintf(stderr, "FAILED: no heap\n");
		return -1;
	}
	// fprintf(stderr, "%p\n", testheap);
	if (freeblock(testheap) != OK) {
		fprintf(stderr, "FAILED: cannot free buddyheap\n");
		return -1;
	}
	srand(time(NULL));
 	for(int i=0; i<ITERS; i++) {
		if (rc == 0) rc = testSimpleAlloc();
		if (rc == 0) rc = testSimpleRealloc();
		if (rc == 0) rc = testFreeByRealloc();
		if (rc == 0) rc = testAllocByRealloc();
		if (rc == 0) rc = testAllocWrongFree();
		if (rc == 0) rc = testNAllocs(100, 100);
		if (rc == 0) rc = testNAllocs(100, 50);
		if (rc == 0) rc = testNAllocs(100, 0);
		if (rc == 0) rc = testNAllocs(750, 50); // slow!
		if (rc == 0) rc = validateptrs();
		if (rc != 0) break;
	}
	if (rc != 0) {
		fprintf(stderr, "FAILED!\n");
		return -1;
	}
	fprintf(stderr, "PASSED!\n");
	fprintf(stderr, "allocs  : %07d\n", allocs);
	fprintf(stderr, "reallocs: %07d\n", reallocs);
	fprintf(stderr, "frees   : %07d\n", frees);
	if (ifrees > 0 && invalid > 0) {
		fprintf(stderr, "invalid: %d%%\n", (invalid*100)/ifrees);
	} else if (ifrees > 0) {
		fprintf(stderr, "no invalid frees\n");
	}
	return 0;
}
