#ifndef PAGING_H
#define PAGING_H

#include "../include/types.h"

/* Builds the kernel's identity map (first PAGING_TABLE_COUNT * 4 MB, supervisor
 * only) in PAGE_DIRECTORY_ADDRESS. Call once, then enable_paging(). */
void     paging_init();
uint32_t paging_kernel_directory();

/* Per-task address space.
 *   - the kernel identity map is shared (same page tables, supervisor-only), so
 *     kernel code, stacks, the heap and VGA are reachable in every address space;
 *   - the APP_VIRT_BASE window maps APP_MEM_SIZE bytes at physical `app_phys`
 *     with user access, in a page table private to this task.
 * Returns false (nothing leaked) if the heap is out of memory. */
bool     paging_create_user_space(uint32_t app_phys, uint32_t* pd_out, uint32_t* pt_out);
void     paging_destroy_user_space(uint32_t pd, uint32_t pt);

#endif
