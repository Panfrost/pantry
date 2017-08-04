/* CBMA -- Cafe's Bad Memory Allocator */

#include <stdio.h>
#include <memory.h>
#include <pandriver.h>

#define CBMA_PAGES 1024
uint32_t cbma_bottom;
uint32_t cbma_top;

void init_cbma(int fd)
{
	cbma_bottom = alloc_gpu_pages(fd, CBMA_PAGES,
			MALI_MEM_PROT_CPU_RD | MALI_MEM_PROT_CPU_WR |
			MALI_MEM_PROT_GPU_RD | MALI_MEM_PROT_GPU_WR |
			MALI_MEM_SAME_VA); cbma_top = cbma_bottom;
	memset((void*) cbma_bottom, 0, CBMA_PAGES << PAGE_SHIFT);
}

void* galloc(size_t sz)
{
	cbma_bottom &= 0xFFFFFC00;
	cbma_bottom += 0x400;
	cbma_bottom += sz;
	return (void*) (uint32_t) (cbma_bottom - sz);
}

void gfree(void* ptr)
{
	printf("gfree %p\n", ptr);
	/* TODO */
}
