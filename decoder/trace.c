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

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pandriver.h>
#include <pantrace.h>

/* TODO: Remove this dependency */

#include "../panwrap/panwrap.h"

/* Assert that synthesised command stream is bit-identical with trace */

static void assert_gpu_same(uint64_t addr, size_t s, uint8_t *synth)
{
	uint8_t *buffer = fetch_mapped_gpu(addr, s);

	if (!buffer) {
		panwrap_log("Bad allocation in assert %" PRIx64 "\n", addr);
		return;
	}

	for (unsigned int i = 0; i < s; ++i) {
		if (buffer[i] != synth[i]) {
			panwrap_log("At %" PRIX64 ", expected:\n", addr);
			panwrap_log_hexdump_trimmed(synth, s, "\t\t");
			panwrap_log("Instead got:\n");
			panwrap_log_hexdump_trimmed(buffer, s, "\t\t");

			break;
		}
	}
}

static void assert_gpu_zeroes(uint64_t addr, size_t s)
{
	uint8_t *zero = calloc(s, 1);
	printf("Zero address %" PRIX64 "\n", addr);
	assert_gpu_same(addr, s, zero);
	free(zero);
}

static void quick_dump_gpu(uint64_t addr, size_t s)
{
	uint8_t *buf;

	if (!addr) {
		panwrap_log("Null quick dump\n");
		return;
	}

	buf = fetch_mapped_gpu(addr, s);

	panwrap_log("Quick GPU dump (%" PRIX64 ")\n", addr);

	if (!buf) {
		panwrap_log("Not found\n");
		return;
	}

	panwrap_log_hexdump_trimmed(buf, s, "\t\t");
}

#include "chai-notes.h"

#define DEFINE_CASE(label) case label: return #label;

static char *chai_job_type_name(int type)
{
	switch (type) {
	DEFINE_CASE(JOB_NOT_STARTED)
	DEFINE_CASE(JOB_TYPE_NULL)
	DEFINE_CASE(JOB_TYPE_SET_VALUE)
	DEFINE_CASE(JOB_TYPE_CACHE_FLUSH)
	DEFINE_CASE(JOB_TYPE_COMPUTE)
	DEFINE_CASE(JOB_TYPE_VERTEX)
	DEFINE_CASE(JOB_TYPE_TILER)
	DEFINE_CASE(JOB_TYPE_FUSED)
	DEFINE_CASE(JOB_TYPE_FRAGMENT)

	default:
		panwrap_log("Requested job type %X\n", type);
		return "UNKNOWN";
	}
}

static char* chai_gl_mode_name(uint8_t b)
{
	switch (b) {
	DEFINE_CASE(CHAI_POINTS)
	DEFINE_CASE(CHAI_LINES)
	DEFINE_CASE(CHAI_TRIANGLES)
	DEFINE_CASE(CHAI_TRIANGLE_STRIP)
	DEFINE_CASE(CHAI_TRIANGLE_FAN)
	default:
		panwrap_log("Unknown mode %X\n", b);
		return "GL_UNKNOWN";
	}
}

/* TODO: Figure out what "fbd" means */
/* TODO: Corresponding SFBD decode (don't assume MFBD) */

static void chai_trace_fbd(uint32_t fbd)
{
	struct tentative_mfbd *mfbd =
		fetch_mapped_gpu(fbd & FBD_POINTER_MASK, sizeof(*mfbd));
	uint8_t *buf;
	uint32_t *buf32;

	panwrap_log("MFBD @ %X (%X)\n",
		    fbd & FBD_POINTER_MASK, fbd & ~FBD_POINTER_MASK);
	panwrap_log("MFBD flags %X, heap free address %" PRIX64 "\n",
		    mfbd->flags, mfbd->heap_free_address);

	panwrap_log_hexdump_trimmed((uint8_t *) mfbd->block1,
				    sizeof(mfbd->block1), "\t\t");

	panwrap_log("unk2\n");
	buf = fetch_mapped_gpu(mfbd->unknown2, 64);
	panwrap_log_hexdump_trimmed(buf, 64, "\t\t");

	assert_gpu_zeroes(mfbd->block2[0], 64);
	assert_gpu_zeroes(mfbd->block2[1], 64);
	assert_gpu_zeroes(mfbd->ugaT, 64);
	assert_gpu_zeroes(mfbd->unknown_gpu_address, 64);

	/* Somehow maybe sort of kind of framebufferish?
	 * It changes predictably in the same way as the FB.
	 * Unclear what exactly it is, though.
	 *
	 * Where the framebuffer is: 1A 33 00 00
	 * This is: 71 B3 03 71 6C 4D 87 46
	 * Where the framebuffer is: 1A 33 1A 00
	 * This is: AB E4 43 9C E8 D6 D1 25
	 *
	 * It repeats, too, but everything 8 bytes rather than 4.
	 *
	 * It is a function of the colour painted. But the exact details
	 * are elusive.
	 *
	 * Also, this is an output, not an input.
	 * Assuming the framebuffer works as intended, RE may be
	 * pointless.
	 */

	panwrap_log("ugaT %" PRIX64 ", uga %" PRIX64 "\n",
		    mfbd->ugaT, mfbd->unknown_gpu_address);
	panwrap_log("ugan %" PRIX64 "\n", mfbd->unknown_gpu_addressN);
	buf = fetch_mapped_gpu(mfbd->unknown_gpu_addressN, 64);
	panwrap_log_hexdump_trimmed(buf, 64, "\t\t");

	panwrap_log("unk1 %X, b1 %" PRIX64 ", b2 %" PRIX64 ", unk2 %" PRIX64 ", unk3 %" PRIX64 ", blah %" PRIX64 "\n",
		    mfbd->unknown1,
		    mfbd->block2[0],
		    mfbd->block2[1],
		    mfbd->unknown2,
		    mfbd->unknown3,
		    mfbd->blah);

	panwrap_log("Weights [ %X, %X, %X, %X, %X, %X, %X, %X ]\n",
		    mfbd->weights[0], mfbd->weights[1],
		    mfbd->weights[2], mfbd->weights[3],
		    mfbd->weights[4], mfbd->weights[5],
		    mfbd->weights[6], mfbd->weights[7]);

	panwrap_log_hexdump_trimmed((uint8_t *) mfbd->block3,
				    sizeof(mfbd->block3), "\t\t");
	panwrap_log("---\n");
	panwrap_log_hexdump_trimmed((uint8_t *) mfbd->block4,
				    sizeof(mfbd->block4), "\t\t");

	panwrap_log("--- (seriously though) --- %X\n", mfbd->block3[4]);
	buf32 = fetch_mapped_gpu(mfbd->block3[4], 128);

	if (buf32) {
		panwrap_log_hexdump_trimmed((uint8_t*) buf32, 128, "\t\t");

		quick_dump_gpu(buf32[6], 64);
		quick_dump_gpu(buf32[20], 64);
		quick_dump_gpu(buf32[23], 64);
		quick_dump_gpu(buf32[24], 64);
		quick_dump_gpu(buf32[25], 64);
		quick_dump_gpu(buf32[26], 64);
		quick_dump_gpu(buf32[27], 64);
		quick_dump_gpu(buf32[28], 64);
		quick_dump_gpu(buf32[31], 64);
	}

	quick_dump_gpu(mfbd->block3[16], 128);
}

static void chai_trace_vecN(float *p, size_t count)
{
	if (count == 1)
		panwrap_log("\t<%f>,\n", p[0]);
	else if (count == 2)
		panwrap_log("\t<%f, %f>,\n", p[0], p[1]);
	else if (count == 3)
		panwrap_log("\t<%f, %f, %f>,\n", p[0], p[1], p[2]);
	else if (count == 4)
		panwrap_log("\t<%f, %f, %f, %f>,\n", p[0], p[1], p[2], p[3]);
	else
		panwrap_log("Cannot print vec%zu\n", count);
}

//#include "shim.c"

static void chai_trace_attribute(uint64_t address)
{
	uint64_t raw;
	uint64_t flags;
	size_t vertex_count;
	size_t component_count;
	float *v;
	float *p;

	struct attribute_buffer *vb =
		(struct attribute_buffer *) fetch_mapped_gpu(
				address,
				sizeof(struct attribute_buffer));
	if (!vb) return;

	vertex_count = vb->total_size / vb->element_size;
	component_count = vb->element_size / sizeof(float);

	raw = vb->elements & ~3;
	flags = vb->elements ^ raw;

	p = v = fetch_mapped_gpu(raw, vb->total_size);

	panwrap_log("attribute vec%zu mem%" PRIX64 "flag%" PRIX64 " = {\n",
		    component_count, raw, flags);

	for (unsigned int i = 0; i < vertex_count; i++, p += component_count) {
		chai_trace_vecN(p, component_count);

		/* I don't like these verts... let's add some flare! */

		p[0] += (float) (rand() & 0xFF) / 1024.0f;
		p[1] += (float) (rand() & 0xFF) / 1024.0f;
		p[2] += (float) (rand() & 0xFF) / 1024.0f;
	}

	panwrap_log("}\n");
}

static void chai_trace_hw_chain(uint64_t chain)
{
	struct job_descriptor_header *h;
	uint8_t *gen_pay;
	u64 next;
	u64 payload;

	/* Trace descriptor */
	h = fetch_mapped_gpu(chain, sizeof(*h));
	if (!h) {
		panwrap_log("Failed to map the job chain %" PRIX64 "\n\n", chain);
		return;
	}

	panwrap_log("%s job, %d-bit, status %X, incomplete %X, fault %" PRIX64 ", barrier %d, index %hX, dependencies (%hX, %hX)\n",
		    chai_job_type_name(h->job_type),
		    h->job_descriptor_size ? 64 : 32,
		    h->exception_status,
		    h->first_incomplete_task,
		    h->fault_pointer,
		    h->job_barrier,
		    h->job_index,
		    h->job_dependency_index_1,
		    h->job_dependency_index_2);

	payload = chain + sizeof(*h);

	switch (h->job_type) {
	case JOB_TYPE_SET_VALUE:
		{
			struct payload_set_value *s;

			s = fetch_mapped_gpu(payload, sizeof(*s));
			panwrap_log("set value -> %" PRIX64 " (%" PRIX64 ")\n",
				    s->out, s->unknown);
			break;
		}

	case JOB_TYPE_VERTEX:
	case JOB_TYPE_TILER:
		{
			FILE *fp;
			struct payload_vertex_tiler32 *v;
			uint64_t *i_shader, s;
			uint8_t *shader;
			char *fn;

			v = fetch_mapped_gpu(payload, sizeof(*v));

			if ((v->shader & 0xFFF00000) == 0x5AB00000) {
				panwrap_log("Job sabotaged\n");
				break;
			}

			/* Mask out lower 128-bit (instruction word) for flags.
			 *
			 * TODO: Decode flags.
			 */

			i_shader = fetch_mapped_gpu(v->shader, sizeof(u64));

			panwrap_log("%s shader @ %" PRIX64 " (flags %" PRIX64 ")\n",
				    h->job_type == JOB_TYPE_VERTEX ?
				    "Vertex" : "Fragment",
				    *i_shader & ~15, *i_shader & 15);

			shader = fetch_mapped_gpu(*i_shader & ~15,
						  0x880 - 0x540);
			panwrap_log_hexdump_trimmed(shader,
						    0x880 - 0x540, "\t\t");

			asprintf(&fn, "shader_%s.bin",
				 h->job_type == JOB_TYPE_VERTEX ?
				 "Vertex" : "Fragment");
			fp = fopen(fn, "wb");
			fwrite(shader, 1, 0x880 - 0x540, fp);
			free(fn);
			fclose(fp);

			/* Trace attribute based on metadata */
			s = v->attribute_meta;

			while (true) {
				attribute_meta_t *attr_meta = fetch_mapped_gpu(
				    s, sizeof(attribute_meta_t));

				if (!HAS_ATTRIBUTE(*attr_meta))
					break;

				panwrap_log("Attribute %" PRIX64 " (flags %" PRIX64 ")\n",
					    ATTRIBUTE_NO(*attr_meta),
					    ATTRIBUTE_FLAGS(*attr_meta));

				chai_trace_attribute(
				    v->attributes + ATTRIBUTE_NO(*attr_meta) *
				    sizeof(struct attribute_buffer));

				s += sizeof(attribute_meta_t);
			}

			if (h->job_type == JOB_TYPE_TILER) {
				panwrap_log(
				    "Drawing in %s\n",
				    chai_gl_mode_name(((uint8_t *) v->block1)[8]));
			}

			assert_gpu_zeroes(v->zeroes, 64);

			if (v->null1 | v->null2 | v->null4)
				panwrap_log("Null tripped?\n");

			panwrap_log("%cFBD\n", v->fbd & FBD_TYPE ? 'M' : 'S');
			chai_trace_fbd(v->fbd);

			panwrap_log_hexdump_trimmed((uint8_t *) v->block1,
						    sizeof(v->block1),
						    "\t\t");

			for (int addr = 0; addr < 14; ++addr) {
				uint32_t address =
					((uint32_t *) &(v->zeroes))[addr];
				uint8_t *buf;
				size_t sz = 64;

				/* Structure known. Skip hex dump */
				if (addr == 2) continue;
				if (addr == 3) continue;
				if (addr == 6) continue;
				if (addr == 10 && h->job_type == JOB_TYPE_VERTEX) continue;
				if (addr == 11) continue;
				if (addr == 12) continue;

				/* Size known exactly but not structure; cull */
				if (addr == 0) sz = 0x100;
				if (addr == 1) sz = 0x10;
				if (addr == 4) sz = 0x40;
				if (addr == 5) sz = 0x20;
				if (addr == 7) sz = 0x20;
				if (addr == 8) sz = 0x20;

				panwrap_log("Addr %d %X\n", addr, address);

				if (!address)
					continue;

				buf = fetch_mapped_gpu(address, sz);

				panwrap_log_hexdump_trimmed(buf, sz, "\t\t");

				if (addr == 8) {
					uint32_t sub =
						*((uint32_t *) buf) & 0xFFFFFFFE;
					uint8_t *sbuf =
						fetch_mapped_gpu(sub, 64);

					panwrap_log("---\n");
					panwrap_log_hexdump_trimmed(
					    sbuf, 64, "\t\t");
				}

				if (addr == 1) {
					uint64_t sub = *((uint64_t*) buf) >> 8;
					uint8_t *sbuf =
						fetch_mapped_gpu(sub, 64);

					panwrap_log("--- %" PRIX64 "\n", sub);
					panwrap_log_hexdump_trimmed(
					    sbuf, 64, "\t\t");
				}

				if (addr == 4 &&
				    h->job_type == JOB_TYPE_TILER) {
					__fp16 *uniforms = (__fp16*) buf;

					printf("uniform vec4 u = vec4(");

					for(int u = 0; u < 4; ++u) {
						float v = (float) uniforms[u];
						printf("%f, ", v);
					}

					printf("\b\b);\n");
				}
			}

			panwrap_log_hexdump_trimmed((uint8_t *) v->block2,
						    sizeof(v->block2), "\t\t");
			break;
	}

	case JOB_TYPE_FRAGMENT: {
		struct payload_fragment *f;

		f = fetch_mapped_gpu(payload, sizeof(*f));

		/* Bit 31 of max_tile_coord clear on the first frame.
		 * Set after.
		 * TODO: Research.
		 */

		panwrap_log("frag %X %X (%d, %d) -> (%d, %d), fbd type %cFBD at %" PRIX64 " (%" PRIX64 ") \n",
				f->min_tile_coord, f->max_tile_coord,
				TILE_COORD_X(f->min_tile_coord),
				TILE_COORD_Y(f->min_tile_coord),
				TILE_COORD_X(f->max_tile_coord),
				TILE_COORD_Y(f->max_tile_coord),
				f->fragment_fbd & FBD_TYPE ? 'M' : 'S',
				f->fragment_fbd,
				f->fragment_fbd & FBD_POINTER_MASK);

		chai_trace_fbd(f->fragment_fbd);

		break;
	}

	default:
		panwrap_log("Dumping payload %" PRIX64 " for job type %s\n",
			    payload,
			    chai_job_type_name(h->job_type));

		gen_pay = fetch_mapped_gpu(payload, 256);
		panwrap_log_hexdump_trimmed(gen_pay, 256, "\t\t");
		break;
	}

	next = h->job_descriptor_size ? h->next_job._64 : h->next_job._32;

	/* Traverse the job chain */
	if (next)
		chai_trace_hw_chain(next);
}

void chai_trace_atom(const struct mali_jd_atom_v2 *v)
{
	if (v->core_req & MALI_JD_REQ_SOFT_JOB) {
		if (v->core_req & MALI_JD_REQ_SOFT_REPLAY) {
			struct mali_jd_replay_payload *payload;

			payload = (struct mali_jd_replay_payload *)
				fetch_mapped_gpu(v->jc, sizeof(*payload));

			panwrap_log(
			    "tiler_jc_list = %" PRIX64 ", fragment_jc = %" PRIX64 ", \nt "
			    "tiler_heap_free = %" PRIX64 ", fragment hierarchy mask = %hX, "
			    "tiler hierachy mask = %hX, hierarchy def weight %X, "
			    "tiler core_req = %X, fragment core_req = %X",
			    payload->tiler_jc_list,
			    payload->fragment_jc,
			    payload->tiler_heap_free,
			    payload->fragment_hierarchy_mask,
			    payload->tiler_hierarchy_mask,
			    payload->hierarchy_default_weight,
			    payload->tiler_core_req,
			    payload->fragment_core_req);
		} else  {
			/* TODO: Soft job decoding */
			panwrap_log("Unknown soft job\n");
		}
	} else {
		chai_trace_hw_chain(v->jc);
	}
}
