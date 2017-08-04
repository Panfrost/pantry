/*
 *
 * (C) COPYRIGHT 2014-2016 ARM Limited. All rights reserved.
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

#ifndef __JOBS_H
#define __JOBS_H

struct job_descriptor_header {
	u32 exception_status;
	u32 first_incomplete_task;
	u64 fault_pointer;
	u8 job_descriptor_size : 1;
	u8 job_type : 7;
	u8 job_barrier : 1;
	u8 _reserved_01 : 1;
	u8 _reserved_1 : 1;
	u8 _reserved_02 : 1;
	u8 _reserved_03 : 1;
	u8 _reserved_2 : 1;
	u8 _reserved_04 : 1;
	u8 _reserved_05 : 1;
	u16 job_index;
	u16 job_dependency_index_1;
	u16 job_dependency_index_2;
	union {
		u64 _64;
		u64 _32;
	} next_job;

} __attribute__((packed));

typedef struct mali_jd_replay_payload {
	/**
	 * Pointer to the first entry in the mali_jd_replay_jc list.  These
	 * will be replayed in @b reverse order (so that extra ones can be added
	 * to the head in future soft jobs without affecting this soft job)
	 */
	u64 tiler_jc_list;

	/**
	 * Pointer to the fragment job chain.
	 */
	u64 fragment_jc;

	/**
	 * Pointer to the tiler heap free FBD field to be modified.
	 */
	u64 tiler_heap_free;

	/**
	 * Hierarchy mask for the replayed fragment jobs. May be zero.
	 */
	u16 fragment_hierarchy_mask;

	/**
	 * Hierarchy mask for the replayed tiler jobs. May be zero.
	 */
	u16 tiler_hierarchy_mask;

	/**
	 * Default weight to be used for hierarchy levels not in the original
	 * mask.
	 */
	u32 hierarchy_default_weight;

	/**
	 * Core requirements for the tiler job chain
	 */
	mali_jd_core_req tiler_core_req;

	/**
	 * Core requirements for the fragment job chain
	 */
	mali_jd_core_req fragment_core_req;

	u8 padding[4];
} mali_jd_replay_payload;

#endif
