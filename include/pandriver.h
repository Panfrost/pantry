/*
 * © Copyright 2017 The BiOpenly Community
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

#ifndef __PANDRIVER_H__
#define __PANDRIVER_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <mali-ioctl.h>
#include <jobs.h>
#include <chai-notes.h>

/* TODO: Find non-hackish memory allocator */

void init_cbma(int fd);
void* galloc(size_t sz);
void gfree(void* ptr);

/* Integer types used in the shim. */

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

/* Thin wrappers around ioctls */

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

/* Raw command stream generation */

struct job_descriptor_header* set_value_helper(uint64_t out);

uint64_t make_mfbd(bool tiler, uint64_t heap_free_address,
		uint64_t scratchpad);

uint32_t job_chain_fragment(int fd, uint64_t framebuffer,
		uint64_t heap_free_address, uint64_t scratchpad);

uint64_t import_shader(int fd, uint8_t *shader, size_t sz, bool frag);
uint32_t upload_vertices(float *vertices, size_t sz);

struct job_descriptor_header* vertex_tiler_helper(int fd, bool tiler,
		uint32_t fbd, uint32_t vertex_buffer,
		uint32_t zero_buffer, uint32_t mode,
		void *shader, size_t shader_size);

uint32_t job_chain_vertex_tiler(int fd,
		float *vertices, size_t vertex_size, int mode,
		void* vertex_shader, size_t vs_sz,
		void *fragment_shader, size_t fs_sz,
		uint64_t heap_free_address, uint64_t scratchpad);

void job_chain_replay(int fd, uint32_t tiler_jc, uint32_t fragment_jc,
		uint64_t heap_free_address, uint64_t framebuffer);

#endif
