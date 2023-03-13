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
 * of each memory block, a constant amount of memory per
 * block is overhead, namely 5 bytes. When the heap is
 * mainly used for large memory blocks, this overhead
 * is negligible.
 *
 * Max address space  :  4 GiB 
 * Max allocation unit:  2 GiB
 * Min allocation unit: 32 Byte
 * -----------------------------------------------------------------------
 */

#ifndef __FFIT_H__
#define __FFIT_H__

#include <stdlib.h>
#include <stdint.h>

#define FFIT_HEAP_FOUND    0x0
#define FFIT_HEAP_NOTFOUND 0x4
#define FFIT_HEAP_INTERNAL -1
#define FFIT_HEAP_OK 0x0

/* ------------------------------------------------------------------------
 * Heap Structure
 * ------------------------------------------------------------------------
 */
typedef struct {
  uintptr_t mh;    // heap address
  size_t    hs;    // heap size
  uint32_t  first; // available list first
  uint32_t  last;  // available list last
} ffit_heap_t;

/* ------------------------------------------------------------------------
 * Heap Initialisation
 * Must be called once per process
 * ------------------------------------------------------------------------
 */
int ffit_init(ffit_heap_t *h);

/* ------------------------------------------------------------------------
 * Get a block of size sz
 * Returns a pointer on success and NULL on failure
 * ------------------------------------------------------------------------
 */
void *ffit_get_block(ffit_heap_t *h, size_t sz);

/* ------------------------------------------------------------------------
 * Free the block indicated by ptr
 * Returns 0 on success -1 or 4 on error.
 * Fails if the address is unknown (4)
 *       or if memory was corrupted (-1)
 * ------------------------------------------------------------------------
 */
int  ffit_free_block(ffit_heap_t *h, void *ptr);

/* ------------------------------------------------------------------------
 * Extend the block indicated by ptr to size sz.
 * If ptr is NULL, the function behaves exactly like ffit_get_block.
 * If sz is 0, the function behaves exactly like ffit_free_block.
 * If sz is greater than the current size of the block,
 *    the block is extended;
 * If sz is less than the current size of the block,
 *    the block is shrunk.
 * If the sz is equal to the current size of the block, ptr is returned.
 * On success, a valid pointer is returned.
 * The block indicated by that pointer contains the same data
 * as the original block. If the the block was shrunk,
 * this is only true for the first sz byte of data.
 * Further, if the new returned pointer differs from the original pointer,
 * the original block was released and is not available anymore.
 * On error, NULL is returned and the original pointer is still valid.
 * In case of an internal error, the flag rc is set to that error.
 * Otherwise, rc is 0. The conditions for setting the rc flag
 * are identical to those in ffit_free_block.
 * ------------------------------------------------------------------------
 */
void *ffit_extend_block(ffit_heap_t *h, void *ptr, size_t sz, int *rc);

/* ------------------------------------------------------------------------
 * Print a visualisation of the current usage of the heap to stdout
 * indicating the size of each block in
 * - red, if the block is currently used
 * - green, if the block is currently available.
 * ------------------------------------------------------------------------
 */
void  ffit_print_heap(ffit_heap_t *h);

/* ------------------------------------------------------------------------
 * Retriev heap statistics.
 * Note that wmark (watermark) and stps (steps)
 * are not computed.
 * ------------------------------------------------------------------------
 */
void  ffit_get_stats(ffit_heap_t  *h,
                     uint32_t   *mem,  // heap size 
                     uint32_t   *usd,  // used memory 
                     uint32_t   *fre); // available memory
#endif
