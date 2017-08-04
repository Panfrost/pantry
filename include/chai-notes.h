/*
 *
 * (C) COPYRIGHT 2010-2016 ARM Limited. All rights reserved.
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

#ifndef __CHAI_NOTES_H
#define __CHAI_NOTES_H

#include <stddef.h>

#include "jobs.h"

/* See corresponding structures in chai/jobs.h */

struct payload_set_value {
	uint64_t out;
	uint64_t unknown;
};

/* From mali_kbase_replay.c */
/* fbd_address & mask */
#define FBD_POINTER_MASK (~0x3f)

/* set for MFBD; otherwise SFBD */
#define FBD_TYPE 1

#define SFBD (0)
#define MFBD (1)

/* appears to be set for fragment; clear for vertex/tiler */

#define FBD_TILER (0)
#define FBD_FRAGMENT (2)

/* From mali_kbase_10969_workaround.c */
#define X_COORDINATE_MASK 0x00000FFF
#define Y_COORDINATE_MASK 0x0FFF0000

/* The above defines as useful macros */
#define TILE_COORD_X(coord) ((coord) & X_COORDINATE_MASK)
#define TILE_COORD_Y(coord) (((coord) & Y_COORDINATE_MASK) >> 16)

#define MAKE_TILE_COORD(X, Y, flag) ((X) | ((Y) << 16) | (flag))

struct payload_fragment {
	uint32_t min_tile_coord;
	uint32_t max_tile_coord;
	uint64_t fragment_fbd;
};

/* Parts from mali_kbase_replay.c. Mostly from RE. */

struct payload_vertex_tiler32 {
	uint32_t block1[11];

	uint32_t zeroes;
	uint32_t unknown1; /* pointer */
	uint32_t null1;
	uint32_t null2;
	uint32_t unknown2; /* pointer */
	uint32_t shader;   /* struct shader_meta */
	uint32_t attributes; /* struct attribute_buffer[] */
	uint32_t attribute_meta; /* attribute_meta[] */
	uint32_t unknown5; /* pointer */
	uint32_t unknown6; /* pointer */
	uint32_t nullForVertex;
	uint32_t null4;
	uint32_t fbd;	   /* struct tentative_fbd */
	uint32_t unknown7; /* pointer */

	uint32_t block2[36];
};

/* FIXME: This is very clearly wrong */

#define SHADER (1 << 0)
#define SHADER_VERTEX (1 << 2)
#define SHADER_FRAGMENT (1 << 3)

struct shader_meta {
	uint64_t shader;
	uint64_t unknown1;
	uint64_t unknown2;
};

struct attribute_buffer {
	uint64_t elements;
	size_t element_size;
	size_t total_size;
};

typedef uint64_t attribute_meta_t;

#define HAS_ATTRIBUTE(meta) (!!meta)
#define ATTRIBUTE_NO(meta) (meta & 0xFF)
#define ATTRIBUTE_FLAGS(meta) (meta & 0xFFFFFFFFFFFFFF00)

/* Synthesis of fragment_job from mali_kbase_reply.c with #defines from
 * errata file */

#define JOB_NOT_STARTED	(0)
#define JOB_TYPE_NULL		(1)
#define JOB_TYPE_SET_VALUE	(2)
#define JOB_TYPE_CACHE_FLUSH	(3)
#define JOB_TYPE_COMPUTE	(4)
#define JOB_TYPE_VERTEX		(5)
#define JOB_TYPE_TILER		(7)
#define JOB_TYPE_FUSED		(8)
#define JOB_TYPE_FRAGMENT	(9)

#define CHAI_POINTS		0x01
#define CHAI_LINES		0x02
#define CHAI_TRIANGLES		0x08
#define CHAI_TRIANGLE_STRIP	0x0A
#define CHAI_TRIANGLE_FAN	0x0C

/* See mali_kbase_replay.c */

#define FBD_HIERARCHY_WEIGHTS 8
#define FBD_HIERARCHY_MASK_MASK 0x1fff

struct tentative_mfbd {
	uint64_t blah;
	uint64_t ugaT;
	uint32_t block1[10];
	uint32_t unknown1;
	uint32_t flags;
	uint64_t block2[2];
	uint64_t heap_free_address;
	uint64_t unknown2;
	uint32_t weights[FBD_HIERARCHY_WEIGHTS];
	uint64_t unknown_gpu_addressN;
	uint32_t block3[22];
	uint64_t unknown_gpu_address;
	uint64_t unknown3;
	uint32_t block4[10];
};

#define JOB_32_BIT 0
#define JOB_64_BIT 1

#endif
