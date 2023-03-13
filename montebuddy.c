/* -----------------------------------------------------------------------
 * Monte Carlo Simulation for Dynamic Memory Management System
 * -----------------------------------------------------------
 * Follows Knuth, Vol. 1, Sec. 2.5.
 *
 * (c) Tobias Schoofs, 2010 -- 2020
 *     This code is in the Public Domain.
 *
 * The simulation runs in iterations ("turns").
 * In each turn an allocation cycle and a free cycle are performed.
 * In the allocation cycle memory is allocated and a random turn
 * is generated that determines the life time of this particular memory.
 * In the free cycle blocks of memory whose life time ends in this turn
 * is freed. Every 2000 iterations, the heap is printed on stdout.
 *
 * Currently, only alloc and free are used;
 * realloc still needs to be added.
 * -----------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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
#include <buddy.h>
char _bheap[2097152];
#include <buddy.h>
#ifdef WITH_EMERGENCY
#define H 1048576
#define E 1
#else
#define H 2097152
#define E 0
#endif
buddy_heap_t h;
#define heapinit() \
	h.mh = (uintptr_t)_bheap; \
        h.hs = H; \
        h.e  = E; \
        rc = buddy_init(&h)
#define getblock(n) buddy_get_block(&h,n)
#define freeblock(n) buddy_free_block(&h,n)
#define exblock(n,s,r) buddy_extend_block(&h,n,s,r)
#define printheap() buddy_print_heap(&h)
#endif

// compute number of iterations
// compute number of pointers
// obtain statics periodically
// remember max heap usage
// pass in heap size
// write script to call it n times

#define MINALLOC 8
#define ALLOCSPERCYCLE 1
#define MAXALLOC 8192
#define PTRS 100000

// pointers == iterations

typedef uint32_t it_t;

it_t its = 1048576 / 100;
it_t inow=0;

typedef struct {
	char *ptr;
	size_t sz;
	it_t   it;
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

int freecycle() {
        if (inow == 0) return 0;
	for(int i=0;i<PTRS;i++) {
		if (ps[i].it > 0 && ps[i].it <= inow) {
			// printf("freeing %d\n", idx);
			int rc = freeblock(ps[i].ptr);
			ps[i].ptr = NULL;
			ps[i].sz  = 0;
			ps[i].it  = 0;
			if (rc != 0) return -1;
			frees++;
		}
	}
	// printf("%d frees on %d allocs\n", frees, allocs);
	return 0;
}

/*
void fillRandom(char *mem, size_t size) {
	for(size_t i=0;i<size;i++) {
		mem[i] = (rand()%25) + 65;
	}
}
*/

int doalloc() {
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

	it_t t = its-inow;
	for(;t>0;t>>=1) {
		it_t k = (it_t)log2((double)t);
		if (k == 0 || (rand()%k) == 0) break;
	}
	t += inow;
	if (t >= its) t=its-1;

	/*
	it_t t = 0;
	while (t == 0) t = rand()%its;
	*/

	/*
	if (t < inow) {
		printf("release is in the past\n");
	} else if (t >= its) {
		printf("release is after doomsday\n");
	}
	*/

	for(;p<PTRS;p++) {
		if (ps[p].ptr == NULL) break;
	}
	if (p == PTRS) {
		printf("no room left\n"); return -1;
	}
	ps[p].ptr = getblock(s);
	ps[p].sz  = s;
	ps[p].it  = t;
	// printf("address of size %04zu: %p (%u)\n", s, ps[p].ptr, (uint32_t)((uintptr_t)ps[p].ptr - (uintptr_t)_heap));
	if (ps[p].ptr == NULL) {
		size_t allocd = s;
		for(int z=0; z<p; z++) {
			allocd += ps[z].sz;
		}
		printf("out of memory: %zu\n", allocd);
		return -1;
	}
	// fillRandom(ps[p].ptr, ps[p].sz);
	allocs++;
	return 0;
}

int allocationcycle() {
	/*
	int c = rand()%ALLOCSPERCYCLE;
	if (c == 0) c++;
	*/
	int c = ALLOCSPERCYCLE;

	for(int i=0; i<c; i++) {
		if (doalloc() != 0) return -1;
	}
	return 0;
}

/*

// reallocation cycle
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
			uint8_t x = rand()%4;
			uint32_t l = nextpow2(ps[idx].sz);
			size_t s = x == 0 ? ps[idx].sz : l << x;
			while (s >= MAXALLOC) s >>= 1;
			void *ptr = buddy_extend_block(ps[idx].ptr, s, &rc);
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
				if (s > 0 || rc != BUDDY_HEAP_OK) return -1;
			} else {
				if (s != ps[idx].sz) {
					reallocs++;
					if (ptr == ps[idx].ptr) preservd++;
					else {
						size_t k = s > ps[idx].sz ?
						               ps[idx].sz : s;
						if (memcmp(ps[idx].ptr, ptr, k) != 0) {
							printf("content not preserved!\n");
							return -1;
						}
					}
					if (s > ps[idx].sz) {
						fillRandom(ps[idx].ptr+
						           ps[idx].sz,
                                                           s-ps[idx].sz);
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
*/

int main() {
	int rc = 0;
	memset(ps, 0, PTRS*sizeof(pointer_t));
	heapinit();
	// find heap:
	_heap = getblock(MINALLOC); freeblock(_heap);
	// printheap();
	srand(time(NULL));

	for(inow=0; inow<its; inow++) {
		if (rc == 0) rc = allocationcycle();
		if (rc == 0) rc = freecycle();
		if (inow%2000 == 0) printheap();
		if (rc != 0) break;
	}
	/*
	for(int i=0; i<PTRS; i++) {
		if (ps[i].it != 0) {
			printf("%d\n", ps[i].it);
		}
	}
	*/

	printheap();
	printf("allocs   : %d\n", allocs);
	printf("frees    : %d\n", frees);
	printf("reallocs : %d\n", reallocs);
	if (reallocs > 0) {
		printf("preserved: %d (= %d%%)\n", preservd, (100*preservd)/reallocs);
	}
	if (rc) printf("FAILED in round %u!!!\n", inow);
	else printf("PASSED after %u iterations!\n", its);
	return rc;
}
