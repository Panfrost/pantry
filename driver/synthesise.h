#include "shim.h"
#include "jobs.h"
#include "memory.h"
#include "chai-notes.h"

#include <stddef.h>
#include <stdbool.h>

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
