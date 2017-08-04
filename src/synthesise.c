/*
 *
 * Copyright (C) 2017 Cafe Beverage. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

#include "synthesise.h"
#include <mali-ioctl.h>

#include <stdlib.h>
#include <memory.h>

#define SV_OFFSET (0x4000)

#define XYZ_COMPONENT_COUNT 3

#define INDEX_FRAGMENT 1

int atom_count = 0;

struct mali_jd_dependency no_dependency = {
	.atom_id = 0,
	.dependency_type = MALI_JD_DEP_TYPE_INVALID
};

struct job_descriptor_header* set_value_helper(uint64_t out)
{
	void* packet = galloc(sizeof(struct job_descriptor_header) +
			sizeof(struct payload_set_value));

	struct job_descriptor_header header = {
		.exception_status = JOB_NOT_STARTED,
		.job_descriptor_size = JOB_64_BIT,
		.job_type = JOB_TYPE_SET_VALUE
	};

	struct payload_set_value payload = {
		.out = out,
		.unknown = 0x03
	};

	memcpy(packet, &header, sizeof(header));
	memcpy(packet + sizeof(header), &payload, sizeof(payload));

	return packet;
}

uint64_t make_mfbd(bool tiler, uint64_t heap_free_address, uint64_t scratchpad)
{
	struct tentative_mfbd *mfbd = galloc(sizeof(struct tentative_mfbd));

	mfbd->block2[0] = scratchpad + SV_OFFSET;
	mfbd->block2[1] = scratchpad + SV_OFFSET + 0x200;
	mfbd->ugaT = scratchpad;
	mfbd->unknown2 = heap_free_address | 0x8000000;
	mfbd->flags = 0xF0;
	mfbd->heap_free_address = heap_free_address;
	mfbd->blah = 0x1F00000000;
	mfbd->unknown1 = 0x1600;

	if(!tiler)
		mfbd->unknown3 = 0xFFFFF8C0;

	mfbd->block1[4] = 0x02D801C2;
	mfbd->block1[6] = 0x02D801C2;

	/* This might not a tiler issue so much as a which-frame issue.
	 * First tiler is 0xFF form. Rest of C021. All fragment C021.
	 * TODO: Investigate!
	 */

	mfbd->block1[7] = tiler ? 0x04001080 : 0x01001080;
	mfbd->block1[8] = tiler ? 0x000000FF : 0xC0210000;
	mfbd->block1[9] = tiler ? 0x3F800000 : 0x00000000;

	uint64_t sab0 = 0x5ABA5ABA;

	uint64_t block3[] = {
		0x0000000000000000,
		0x0000000000030005,
		sab0,
		mfbd->block2[0],
		0x0000000000000003,
		0x0000000000000000,
		0x0000000000000000,
		0x0000000000000000,
		sab0 + 0x300,
	};

	memcpy(mfbd->block3, block3, sizeof(block3));

	return (uint32_t) mfbd | MFBD | (tiler ? FBD_TILER : FBD_FRAGMENT);
}

uint32_t job_chain_fragment(int fd, uint64_t framebuffer,
		uint64_t heap_free_address, uint64_t scratchpad)
{
	void* packet = galloc(sizeof(struct job_descriptor_header)
			+ sizeof(struct payload_fragment));

	struct job_descriptor_header header = {
		.exception_status = JOB_NOT_STARTED,
		.job_descriptor_size = JOB_32_BIT,
		.job_type = JOB_TYPE_FRAGMENT,
		.job_index = INDEX_FRAGMENT,
	};

	struct payload_fragment payload = {
		.min_tile_coord = MAKE_TILE_COORD(0, 0, 0),
		.max_tile_coord = MAKE_TILE_COORD(29, 45, 0),
		.fragment_fbd = make_mfbd(false, heap_free_address, scratchpad)
	};

	memcpy(packet, &header, sizeof(header));
	memcpy(packet + sizeof(header), &payload, sizeof(payload));

	struct mali_jd_dependency depTiler = {
		.atom_id = atom_count /* last one */,
		.dependency_type = MALI_JD_DEP_TYPE_DATA
	};

	uint64_t* resource = calloc(sizeof(u64), 1);
	resource[0] = framebuffer | MALI_EXT_RES_ACCESS_EXCLUSIVE;

	/* TODO: free resource */

	struct mali_jd_atom_v2 job = {
		.jc = (uint32_t) packet,
		.ext_res_list = (struct mali_external_resource*) resource /* TODO */,
		.nr_ext_res = 1,
		.core_req = MALI_JD_REQ_EXTERNAL_RESOURCES | MALI_JD_REQ_FS,
		.atom_number = ++atom_count,
		.prio = MALI_JD_PRIO_MEDIUM,
		.device_nr = 0,
		.pre_dep = { depTiler, no_dependency }
	};

	submit_job(fd, job);

	return (uint32_t) packet;
}

uint64_t import_shader(int fd, uint8_t *shader, size_t sz, bool fragment)
{
	int pages = 1 + (sz >> PAGE_SHIFT);

	uint64_t gpu = alloc_gpu_pages(fd, pages, MALI_MEM_PROT_CPU_RD |
			MALI_MEM_PROT_CPU_WR | MALI_MEM_PROT_GPU_RD |
			MALI_MEM_PROT_GPU_EX);

	uint8_t *cpu = mmap_gpu(fd, gpu, pages);

	memcpy(cpu, shader, sz);

	/* TODO: munmap */

	return gpu | SHADER | (fragment ? SHADER_FRAGMENT : SHADER_VERTEX);
}

uint32_t upload_vertices(float *vertices, size_t sz)
{
	struct attribute_buffer *vb;
	vb = (struct attribute_buffer*) galloc(sizeof(*vb));
	
	float *verts = (float*) galloc(sz);
	memcpy(verts, vertices, sz);
	vb->elements = (uint64_t) (uintptr_t) verts;

	vb->element_size = sizeof(float) * XYZ_COMPONENT_COUNT; 
	vb->total_size = sz;

	vb->elements |= 1; /* TODO flags */
	
	return (uint32_t) vb;
}

struct job_descriptor_header* vertex_tiler_helper(int fd, bool tiler,
		uint32_t fbd, uint32_t vertex_buffer,
		uint32_t zero_buffer, uint32_t mode,
		void *shader, size_t shader_size)
{
	void* packet = galloc(sizeof(struct job_descriptor_header)
			+ sizeof(struct payload_vertex_tiler32));

	struct job_descriptor_header header = {
		.exception_status = JOB_NOT_STARTED,
		.job_descriptor_size = JOB_32_BIT,
		.job_type = tiler ? JOB_TYPE_TILER : JOB_TYPE_VERTEX
	};

	/* TODO */
	uint32_t mode_gooks = 0x14000000 | (tiler ? (0x030000 | mode) : 0);
	uint32_t other_gook = tiler ? 0x00000003 : 0x00000000;

	struct payload_vertex_tiler32 payload = {
		.block1 = {
			0x00000003, 0x28000000, mode_gooks, 0x00000000,
			0x00000000, other_gook, 0x00000000, 0x00000000,
			0x00000005, 0x00000000, 0x00000000
		},
		.zeroes = zero_buffer,
		.unknown1 = (uint32_t) galloc(16),
		.null1 = 0,
		.null2 = 0,
		.unknown2 = (uint32_t) galloc(32),
		.shader = (uint32_t) galloc(sizeof(struct shader_meta)),
		.attributes = vertex_buffer,
		.attribute_meta = (uint32_t) galloc(16), /* TODO */
		.unknown5 = (uint32_t) galloc(32),
		.unknown6 = (uint32_t) galloc(64),
		.nullForVertex = tiler ? (uint32_t) galloc(64) : 0,
		.null4 = 0,
		.fbd = fbd,
		.unknown7 = tiler ? 0 : ((uint32_t) galloc(64) | 1) /* TODO */
	};

	struct shader_meta *s = (struct shader_meta*) payload.shader;
	s->shader = import_shader(fd, shader, shader_size, tiler);

	if(!tiler) {
		uint32_t ni[] = {
			0x43200000, 0x42F00000, 0x3F000000, 0x00000000,
			0x43200000, 0x42F00000, 0x3F000000, 0x00000000
		};

		memcpy((void*) payload.unknown2, ni, sizeof(ni));
	}

	if(tiler) {
		/* Lose precision... on purpose? */
		payload.unknown7 = (uint32_t) s->shader;
	}

	payload.unknown7 = tiler ? 0xDEADBA00 : 0xDEADFA00;

	/* TODO: Decode me! */

	if(tiler) {
		s->unknown1 = 0x0007000000000000;
		s->unknown2 = 0x0000000000020602;
	} else {
		s->unknown1 = 0x0005000100000000;
		s->unknown2 = 0x0000000000420002;
	}

	/* TODO: Generate on the fly (see trace.c) */
	uint32_t *p = (uint32_t*) payload.attribute_meta;
	*p = 0x2DEA2200;

	/* I have *no* idea */

	uint64_t pi[] = {
		0x0000000017E49000, 0x0000000017E49000, 
		0x0000000017E49000, 0x0000000017E49000, 
		0x00000000179A2200, 0x0000000017E49000, 
		0x0000000017E49000
	};

	memcpy((void*) payload.unknown6, pi, sizeof(pi));

	if(tiler) {
		uint32_t ni[] = {
			0xFF800000, 0xFF800000,
			0x7F800000, 0x7F800000,
			0x00000000, 0x3F800000,
			0x00000000, 0x00EF013F,
			0x00000000, 0x0000001F,
			0x02020000, 0x00000001
		};

		memcpy((void*) payload.nullForVertex, ni, sizeof(ni));
	}

	/* Use some magic numbers from the traces */
	uint64_t* unk1 = (uint64_t*) payload.unknown1;
	/* unk1[0] = 0x000000B296271001;
	unk1[1] = 0x000000B296273000; */

	unk1[0] = 0x5a5a5a5a5a5a1001;
	unk1[1] = 0x5a5a5a5a5a5a3000;

	uint32_t writeBuffer = (uint32_t) galloc(64);

	uint64_t* unk5 = (uint64_t*) payload.unknown5;
	unk5[0] = ((uint64_t) (tiler ? 0xDB : 0x7A) << 56) | writeBuffer | 1;
	unk5[1] = 0x0000004000000010;

	if(tiler) {
		uint32_t ni[] = {
			0x00000001, 0x00000000, 0x00070000, 0x00020602,
			0x00000000, 0x00000000, 0x00000000, 0x3712FFFF,
			0x44F0FFFF, 0x0007FF00, 0x0007FF00, 0x00000000,
			0x00000000, 0x00000000, 0x00000000, 0x00000200,
			0x00000000, 0xF0122122, 0x00000000, 0x00000000,
			0x00000000, 0xF0122122, 0x00000000, 0xFF800000,
			0xFF800000, 0x7F800000, 0x7F800000, 0x00000000,
			0x3F800000, 0x00000000, 0xEF013F00, 0x00000000,
			0x0000001F, 0x02020000, 0x00000001, 0x00000000
		};

		memcpy(payload.block2, ni, sizeof(ni));
	} else {
		uint32_t ni[] = {
			0x00000000, 0x0000000C, 0x00000030, 0x2DEA2200,
			0x00000000, 0x00000000, 0x00000000, /* Address to 1 */ 0xCAFEDA01,
			0x57000000, 0x00000010, 0x00000040, 0x17E49000,
			0x00000000, 0x17E49000, 0x00000000, 0x17E49000,
			0x00000000, 0x17E49000, 0x00000000, 0x179A2200,
			0x00000000, 0x17E49000, 0x00000000, 0x17E49000,
			0x00000000, 0x00000000, 0x00000000, 0x43200000,
			0x42F00000, 0x3F000000, 0x00000000, 0x43200000,
			0x42F00000, 0x3F000000, 0x00000000, 0x00000000
		};

		memcpy(payload.block2, ni, sizeof(ni));
	}

	/* Trap tiler job execution */

	if(tiler) {
		payload.shader = 0x5AB00A05;

		/* Hit second */
		//payload.zeroes = 0x5AB01A00;

		payload.unknown1 = 0x5AB02A00;
		payload.unknown2 = 0x5AB03A00;
		payload.attributes = 0x5AB04A00;
		payload.attribute_meta = 0x5AB05A00;
		payload.unknown5 = 0x5AB06A00;
		payload.unknown6 = 0x5AB07A00;
		payload.unknown7 = 0x5AB0DA00;

		/* Hit third */
		//payload.fbd	 = 0x5AB09A00;

		/* Hit first */
		// payload.nullForVertex = 0x5AB08A00;
	}

	memcpy(packet, &header, sizeof(header));
	memcpy(packet + sizeof(header), &payload, sizeof(payload));

	return packet;
}

uint32_t job_chain_vertex_tiler(int fd,
		float *vertices, size_t vertex_size, int mode,
		void* vertex_shader, size_t vs_sz,
		void *fragment_shader, size_t fs_sz,
		uint64_t heap_free_address, uint64_t scratchpad)
{
	uint32_t vertex_buffer = upload_vertices(vertices, vertex_size);
	uint32_t vertex_fbd = (uint32_t) make_mfbd(true, heap_free_address, scratchpad);

	uint32_t zero_buffer = (uint32_t) alloc_gpu_pages(fd, 0x20,
			0x3800 | MALI_MEM_PROT_CPU_RD |
			MALI_MEM_PROT_CPU_WR | MALI_MEM_PROT_GPU_RD);

	struct job_descriptor_header *set = set_value_helper(scratchpad + SV_OFFSET);

	struct job_descriptor_header *vertex =
		vertex_tiler_helper(fd, false,
				vertex_fbd, vertex_buffer,
				zero_buffer, mode,
				vertex_shader, vs_sz);

	struct job_descriptor_header *tiler =
		vertex_tiler_helper(fd, true,
				vertex_fbd, vertex_buffer,
				zero_buffer, mode,
				fragment_shader, fs_sz);

	set->next_job._32 = (uint32_t) vertex;
	vertex->next_job._32 = (uint32_t) tiler;

	/* TODO: Determine if these numbers are meaningful */
	set->job_index = 3;
	vertex->job_index = 1;
	tiler->job_index = 2;

	vertex->job_dependency_index_2 = set->job_index;
	tiler->job_dependency_index_1 = vertex->job_index;

	struct mali_jd_atom_v2 job = {
		.jc = (uint32_t) set,
		.ext_res_list = NULL,
		.nr_ext_res = 0,
		.core_req = MALI_JD_REQ_CS | MALI_JD_REQ_T
			| MALI_JD_REQ_CF | MALI_JD_REQ_COHERENT_GROUP,
		.atom_number = ++atom_count,
		.prio = MALI_JD_PRIO_MEDIUM,
		.device_nr = 0,
		.pre_dep = { no_dependency, no_dependency }
	};

	submit_job(fd, job);

	return (uint32_t) tiler;
}

void job_chain_replay(int fd, uint32_t tiler_jc, uint32_t fragment_jc,
		uint64_t heap_free_address, uint64_t framebuffer)
{
	struct mali_jd_replay_payload *payload;

	payload = (struct mali_jd_replay_payload*) galloc(sizeof(*payload));

	payload->tiler_jc_list = tiler_jc;
	payload->fragment_jc = fragment_jc;
	payload->tiler_heap_free = heap_free_address;
	payload->fragment_hierarchy_mask = 0;
	payload->tiler_hierarchy_mask = 0;
	payload->hierarchy_default_weight = 0x10000;
	payload->tiler_core_req = MALI_JD_REQ_T | MALI_JD_REQ_COHERENT_GROUP;
	payload->fragment_core_req = MALI_JD_REQ_FS;

	struct mali_jd_dependency depFragment = {
		.atom_id = atom_count,
		.dependency_type = MALI_JD_DEP_TYPE_DATA
	};

	uint64_t* resource = malloc(sizeof(u64) * 1);
	resource[0] = framebuffer | MALI_EXT_RES_ACCESS_EXCLUSIVE;

	struct mali_jd_atom_v2 job = {
		.jc = (uint32_t) payload,
		.ext_res_list = (struct mali_external_resource*)resource,
		.nr_ext_res = 1,
		.core_req = MALI_JD_REQ_EXTERNAL_RESOURCES | MALI_JD_REQ_SOFT_REPLAY,
		.atom_number = ++atom_count,
		.prio = MALI_JD_PRIO_LOW,
		.device_nr = 0,
		.pre_dep = { depFragment, no_dependency }
	};

	submit_job(fd, job);
}
