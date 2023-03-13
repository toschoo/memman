/* -----------------------------------------------------------------------
 * Smoke test for Dynamic Memory Management Systems
 * -----------------------------------------------------------
 *
 * (c) Tobias Schoofs, 2010 -- 2020
 *     This code is in the Public Domain.
 *
 * The test performs a number of allocations, reallocations and frees,
 * checks a number of conditions (e.g. no overlapping regions) and
 * prints the heap to stdout.
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
	ffit_init(&h)

#define getblock(n) ffit_get_block(&h,n)
#define freeblock(n) ffit_free_block(&h,n)
#define exblock(n,s,r) ffit_extend_block(&h,n,s,r)
#define printheap() ffit_print_heap(&h)

#else
char _bheap[2097152];
#include <buddy.h>
buddy_heap_t h;
#ifdef WITH_EMERGENCY
#define E 1
#else
#define E 0
#endif
#define heapinit() \
	h.mh = (uintptr_t)_bheap; \
	h.hs = 2097152; \
	h.e  = E; \
	buddy_init(&h)

#define getblock(n) buddy_get_block(&h,n)
#define freeblock(n) buddy_free_block(&h,n)
#define exblock(n,s,r) buddy_extend_block(&h,n,s,r)
#define printheap() buddy_print_heap(&h)
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

void *_heap;

int allocs = 0;
int frees  = 0;
int reallocs = 0;
int preservd = 0;

static inline uint32_t nextpow2(uint32_t sz) {
	sz--;
	sz |= sz >> 1;
	sz |= sz >> 2;
	sz |= sz >> 4;
	sz |= sz >> 8;
	sz |= sz >> 16;
	return (sz+1);
}

int myfree() {
	int mx = PTRS/10;
	if (mx < 2) mx = 2;
	for(int i=0;i<mx;i++) {
		int idx = rand()%PTRS;
		if (ps[idx].ptr != NULL) {
			// printf("freeing %p\n", ps[idx].ptr);
			int rc = freeblock(ps[idx].ptr);
			ps[idx].ptr = NULL;
			ps[idx].sz  = 0;
			if (rc != 0) {
				printf("freeblock failed\n");
				return -1;
			}
			frees++;
			break;
		}
	}
	return 0;
}

void fillRandom(char *mem, size_t size) {
	for(size_t i=0;i<size;i++) {
		mem[i] = (rand()%25) + 65;
	}
}

int myreallocate() {
	int p=0;
	for(;p<PTRS;p++) {
		if (ps[p].ptr == NULL) break;
	}
	if (p == PTRS) {
		printf("no room left\n"); return -1;
	}
	int mx = PTRS/10;
	if (mx < 2) mx = 2;
	for(int i=0;i<mx;i++) {
		int idx = rand()%PTRS;
		if (ps[idx].ptr != NULL) {
			int rc = 0;
			uint8_t x = rand()%MINALLOC;
			uint32_t l = nextpow2(ps[idx].sz);
			size_t s = x == 0 ? ps[idx].sz : l << x;
			while (s >= MAXALLOC) s >>= 1;
			void *ptr = exblock(ps[idx].ptr, s, &rc);

			/*
			printf("realloc %d: %p ?= %p, %u != %u \n", idx,
			                                ps[idx].ptr, ptr,
			                                 l, nextpow2(s));
			*/
			
			if (ptr == NULL) {
				if (s > 0) {
					size_t allocd = s;
					for(int z=0; z<p; z++) {
						allocd += ps[z].sz;
					}
					printf("out of memory: %zu\n", allocd);
				} else {
					frees++;
				}
				ps[idx].ptr = NULL;
				ps[idx].sz  = 0;
				if (s > 0 || rc != 0) return -1;
			} else {
				if (s != ps[idx].sz) {
					reallocs++;
					if (ptr == ps[idx].ptr) preservd++;
					else {
						/*
						size_t k = s > ps[idx].sz ?
						               ps[idx].sz : s;
						if (memcmp(ps[idx].ptr, ptr, k) != 0) {
							printf("content not preserved (%zu)!\n", k);
							((char*)ps[idx].ptr)[k] = 0;
							((char*)ptr)[k] = 0;
							printf("%s\n", (char*)ps[idx].ptr);
							printf("%s\n", (char*)ptr);
							return -1;
						}
						*/
					}
					if (s > ps[idx].sz) {
						fillRandom(ptr, s);
					}
				}
				ps[idx].ptr = ptr;
				ps[idx].sz  = s;
			}
			break;
		}
	}
	return 0;
}

int myallocate() {
	int p = 0;
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

	for(;p<PTRS;p++) {
		if (ps[p].ptr == NULL) break;
	}
	if (p == PTRS) {
		printf("no room left\n"); return -1;
	}
	ps[p].ptr = getblock(s);
	ps[p].sz  = s;
	// printf("address of size %04zu: %p (%u)\n", s, ps[p].ptr, (uint32_t)((uintptr_t)ps[p].ptr - (uintptr_t)_heap));
	if (ps[p].ptr == NULL) {
		size_t allocd = s;
		for(int z=0; z<p; z++) {
			allocd += ps[z].sz;
		}
		printf("out of memory: %zu\n", allocd);
		return -1;
	}
	fillRandom(ps[p].ptr, ps[p].sz);
	allocs++;
	return 0;
}

int main() {
	int rc = 0;
	memset(ps, 0, PTRS*sizeof(pointer_t));
	heapinit();
	// find heap:
	_heap = getblock(MINALLOC); freeblock(_heap);
	// buddy_print_heap();
	srand(time(NULL));
	for(int i=0; i<ITERS; i++) {
		int x = rand()%4;
		switch(x) {
		case 0: rc = myfree(); break;
		case 1: rc = myreallocate(); break;
		default: rc = myallocate();
		}
		if (rc != 0) break;
	}
	if (rc == 0) {
		for(int i=0; i<ITERS; i++) {
			for(int z=i+1;z<ITERS;z++) {
				if (ps[i].ptr == NULL) continue;
				if (ps[i].ptr == ps[z].ptr) {
					printf("reused pointer: %p = %p\n", ps[i].ptr, ps[z].ptr);
					rc = -1; break;
				}
				if (ps[i].ptr + ps[i].sz > ps[z].ptr &&
				    ps[i].ptr < ps[z].ptr + ps[z].sz) {
					printf("%p + %zu  overlaps %p + %zu\n", ps[i].ptr, ps[i].sz,
					                                        ps[z].ptr, ps[z].sz);
					rc = -1; break;
				}
			}
			if (rc) break;
		}
	}
	printheap();
	printf("allocs   : %d\n", allocs);
	printf("frees    : %d\n", frees);
	printf("reallocs : %d\n", reallocs);
	if (reallocs > 0) {
		printf("preserved: %d (= %d%%)\n", preservd, (100*preservd)/reallocs);
	}
	if (rc) printf("FAILED!!!\n"); else printf("PASSED!\n");
	return rc;
}
