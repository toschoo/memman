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
 * Max address space  : 4 GiB
 * Max allocation unit: 2 GiB
 * Min allocation unit: 8 Byte
 * -----------------------------------------------------------------------
 */
#ifndef __BUDDY_H__
#define __BUDDY_H__

#include <stdlib.h>
#include <stdint.h>
#include <ffit.h>

#define BUDDY_HEAP_FOUND    0x0
#define BUDDY_HEAP_NOTFOUND 0x4
#define BUDDY_HEAP_INTERNAL -1
#define BUDDY_HEAP_OK 0x0

/* ------------------------------------------------------------------------
 * Main Heap Structure
 * ------------------------------------------------------------------------
 */
typedef struct {
  uintptr_t    mh; // main heap address         (set by user)
  size_t       hs; // overall heap size         (set by user)
  uint8_t       e; // with emergency heap, 0/1  (set by user)
  uintptr_t    eh; // emergency heap            (computed internally)                              
  uint32_t    *ah; // available lists           (computed internally)
  uint8_t     *sh; // size area                 (computed internally)
  uint32_t  msize; // size of main heap         (computed internally)
  uint32_t  asize; // size of available lists   (computed internally)
  uint32_t  ssize; // size of size area         (computed internally)
  uint32_t  esize; // size of emergency heap    (computed internally)
  uint8_t    AMAX; // max available list        (computed internally)
  ffit_heap_t ffh; // emergency heap descriptor (computed internally)
} buddy_heap_t;

/* ------------------------------------------------------------------------
 * Heap Initialisation
 * Must be called once per process
 * ------------------------------------------------------------------------
 */
int buddy_init(buddy_heap_t *h);

/* ------------------------------------------------------------------------
 * Get a block of size sz
 * Returns a pointer on success and NULL on failure
 * ------------------------------------------------------------------------
 */
void *buddy_get_block(buddy_heap_t *h, size_t sz);

/* ------------------------------------------------------------------------
 * Free the block indicated by ptr
 * Returns 0 on success -1 or 4 on error.
 * Fails if the address is unknown (4)
 *       or if memory was corrupted (-1)
 * ------------------------------------------------------------------------
 */
int  buddy_free_block(buddy_heap_t *h, void *ptr);

/* ------------------------------------------------------------------------
 * Extend the block indicated by ptr to size sz.
 * If ptr is NULL, the function behaves exactly like buddy_get_block.
 * If sz is 0, the function behaves exactly like buddy_free_block.
 * If sz is greater than the current size of the block,
 *    the block is extended;
 * If sz is less than the current size of the block,
 *    the block is shrunk.
 * If the sz is equal to the current size of the block, ptr is returned
 *    without any other action taking place.
 * On success, a valid pointer is returned.
 * The block indicated by that pointer contains the same data
 * as the original block. If the block was shrunk,
 * this is only true for the first sz byte of data.
 * Further, if the returned pointer differs from the original pointer,
 * the original block was released and is not available anymore.
 * On error, NULL is returned and the original pointer is still valid.
 * In case of an internal error, the flag rc is set to that error.
 * Otherwise, rc is 0. The conditions for setting the rc flag
 * are identical to the error conditions in buddy_free_block.
 * ------------------------------------------------------------------------
 */
void *buddy_extend_block(buddy_heap_t *h,
           void *ptr, size_t sz, int *rc);

/* ------------------------------------------------------------------------
 * Print a visualisation of the current usage of the heap to stdout
 * indicating the size of each block in
 * - red, if the block is currently used
 * - green, if the block is currently available.
 * ------------------------------------------------------------------------
 */
void  buddy_print_heap(buddy_heap_t *h);

/* ------------------------------------------------------------------------
 * Retriev heap statistics.
 * Note that wmark (watermark) and stps (steps)
 * are not computed.
 * ------------------------------------------------------------------------
 */
void  buddy_get_stats(buddy_heap_t  *h,
                      uint32_t    *mem,  // heap size 
                      uint32_t    *usd,  // used memory 
                      uint32_t    *fre); // available memory
#endif
