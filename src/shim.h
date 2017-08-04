/* The shim requires assert support, with its own fancy name. */

#include <assert.h>
#define CDBG_ASSERT assert

/* Integer types used in the shim. */

#include <stddef.h>
#include <stdint.h>
#include <mali-ioctl.h>

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* MMU-related defines, normally present in Linux, but absent here.
 * See arch/arm/include/asm/page.h in the Linux tree */

#define PAGE_SHIFT	12
#define PAGE_SIZE 	(1 << PAGE_SHIFT)
#define PAGE_MASK 	(~(PAGE_SIZE - 1))

/* Include definitions for thin chai wrappers */

int open_kernel_module();
uint64_t alloc_gpu_pages(int fd, int pages, int e_flags);
uint64_t alloc_gpu_heap(int fd, int pages);
void free_gpu(int fd, uint64_t addr);
void sync_gpu(int fd, uint8_t* cpu, uint64_t gpu, size_t size);
void submit_job(int fd, struct mali_jd_atom_v2 atom);
void flush_job_queue(int fd);
uint8_t* mmap_gpu(int fd, uint64_t addr, int page_count);
void stream_create(int fd, char *stream);
void query_gpu_props(int fd);
