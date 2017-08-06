/*
 * © Copyright 2017 The BiOpenly Community
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
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/ioctl.h>
#include <math.h>
#include <sys/mman.h>
#include <stdarg.h>

#include <mali-ioctl.h>
#include <list.h>
#include "panwrap.h"

static pthread_mutex_t l = PTHREAD_MUTEX_INITIALIZER;

#define LOCK()   pthread_mutex_lock(&l)
#define UNLOCK() pthread_mutex_unlock(&l)

#define IOCTL_CASE(request) (_IOWR(_IOC_TYPE(request), _IOC_NR(request), \
				   _IOC_SIZE(request)))

struct ioctl_info {
	const char *name;
};

struct device_info {
	const char *name;
	const struct ioctl_info info[MALI_IOCTL_TYPE_COUNT][_IOC_NR(0xffffffff)];
};

struct allocated_memory {
	u64 gpu_va;
	bool same_va;
	struct list node;
};

struct mapped_memory {
	size_t length;

	void *addr;
	u64 gpu_va;

	struct list node;
};

typedef void* (mmap_func)(void *, size_t, int, int, int, loff_t);
typedef int (open_func)(const char *, int flags, ...);

#define IOCTL_TYPE(type) [type - MALI_IOCTL_TYPE_BASE] =
#define IOCTL_INFO(n) [_IOC_NR(MALI_IOCTL_##n)] = { .name = #n }
static struct device_info mali_info = {
	.name = "mali",
	.info = {
		IOCTL_TYPE(0x80) {
			IOCTL_INFO(GET_VERSION),
		},
		IOCTL_TYPE(0x82) {
			IOCTL_INFO(MEM_ALLOC),
			IOCTL_INFO(MEM_IMPORT),
			IOCTL_INFO(MEM_COMMIT),
			IOCTL_INFO(MEM_QUERY),
			IOCTL_INFO(MEM_FREE),
			IOCTL_INFO(MEM_FLAGS_CHANGE),
			IOCTL_INFO(MEM_ALIAS),
			IOCTL_INFO(SYNC),
			IOCTL_INFO(POST_TERM),
			IOCTL_INFO(HWCNT_SETUP),
			IOCTL_INFO(HWCNT_DUMP),
			IOCTL_INFO(HWCNT_CLEAR),
			IOCTL_INFO(GPU_PROPS_REG_DUMP),
			IOCTL_INFO(FIND_CPU_OFFSET),
			IOCTL_INFO(GET_VERSION_NEW),
			IOCTL_INFO(SET_FLAGS),
			IOCTL_INFO(SET_TEST_DATA),
			IOCTL_INFO(INJECT_ERROR),
			IOCTL_INFO(MODEL_CONTROL),
			IOCTL_INFO(KEEP_GPU_POWERED),
			IOCTL_INFO(FENCE_VALIDATE),
			IOCTL_INFO(STREAM_CREATE),
			IOCTL_INFO(GET_PROFILING_CONTROLS),
			IOCTL_INFO(SET_PROFILING_CONTROLS),
			IOCTL_INFO(DEBUGFS_MEM_PROFILE_ADD),
			IOCTL_INFO(JOB_SUBMIT),
			IOCTL_INFO(DISJOINT_QUERY),
			IOCTL_INFO(GET_CONTEXT_ID),
			IOCTL_INFO(TLSTREAM_ACQUIRE_V10_4),
			IOCTL_INFO(TLSTREAM_TEST),
			IOCTL_INFO(TLSTREAM_STATS),
			IOCTL_INFO(TLSTREAM_FLUSH),
			IOCTL_INFO(HWCNT_READER_SETUP),
			IOCTL_INFO(SET_PRFCNT_VALUES),
			IOCTL_INFO(SOFT_EVENT_UPDATE),
			IOCTL_INFO(MEM_JIT_INIT),
			IOCTL_INFO(TLSTREAM_ACQUIRE),
		},
	},
};
#undef IOCTL_INFO
#undef IOCTL_TYPE

static inline const struct ioctl_info *
ioctl_get_info(unsigned long int request)
{
	return &mali_info.info[_IOC_TYPE(request) - MALI_IOCTL_TYPE_BASE]
	                      [_IOC_NR(request)];
}

static int mali_fd = 0;
static LIST_HEAD(allocations);
static LIST_HEAD(mmaps);

#define FLAG_INFO(flag) { MALI_MEM_##flag, #flag }
static const struct panwrap_flag_info mem_flag_info[] = {
	FLAG_INFO(PROT_CPU_RD),
	FLAG_INFO(PROT_CPU_WR),
	FLAG_INFO(PROT_GPU_RD),
	FLAG_INFO(PROT_GPU_WR),
	FLAG_INFO(PROT_GPU_EX),
	FLAG_INFO(GROW_ON_GPF),
	FLAG_INFO(COHERENT_SYSTEM),
	FLAG_INFO(COHERENT_LOCAL),
	FLAG_INFO(CACHED_CPU),
	FLAG_INFO(SAME_VA),
	FLAG_INFO(NEED_MMAP),
	FLAG_INFO(COHERENT_SYSTEM_REQUIRED),
	FLAG_INFO(SECURE),
	FLAG_INFO(DONT_NEED),
	FLAG_INFO(IMPORT_SHARED),
	{}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_JD_REQ_##flag, #flag }
static const struct panwrap_flag_info jd_req_flag_info[] = {
	FLAG_INFO(FS),
	FLAG_INFO(CS),
	FLAG_INFO(T),
	FLAG_INFO(CF),
	FLAG_INFO(V),
	FLAG_INFO(FS_AFBC),
	FLAG_INFO(EVENT_COALESCE),
	FLAG_INFO(COHERENT_GROUP),
	FLAG_INFO(PERMON),
	FLAG_INFO(EXTERNAL_RESOURCES),
	FLAG_INFO(ONLY_COMPUTE),
	FLAG_INFO(SPECIFIC_COHERENT_GROUP),
	FLAG_INFO(EVENT_ONLY_ON_FAILURE),
	FLAG_INFO(EVENT_NEVER),
	FLAG_INFO(SKIP_CACHE_START),
	FLAG_INFO(SKIP_CACHE_END),
	{}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { flag, #flag }
static const struct panwrap_flag_info mmap_prot_flag_info[] = {
	FLAG_INFO(PROT_EXEC),
	FLAG_INFO(PROT_READ),
	FLAG_INFO(PROT_WRITE),
	{}
};

/* Fix issue building for Linux */

#ifndef MAP_UNINITIALIZED
#define MAP_UNINITIALIZED 0xDEADBEEF
#endif

static const struct panwrap_flag_info mmap_flags_flag_info[] = {
	FLAG_INFO(MAP_SHARED),
	FLAG_INFO(MAP_PRIVATE),
	FLAG_INFO(MAP_ANONYMOUS),
	FLAG_INFO(MAP_DENYWRITE),
	FLAG_INFO(MAP_FIXED),
	FLAG_INFO(MAP_GROWSDOWN),
	FLAG_INFO(MAP_HUGETLB),
	FLAG_INFO(MAP_LOCKED),
	FLAG_INFO(MAP_NONBLOCK),
	FLAG_INFO(MAP_NORESERVE),
	FLAG_INFO(MAP_POPULATE),
	FLAG_INFO(MAP_STACK),
	FLAG_INFO(MAP_UNINITIALIZED),
	{}
};

static const struct panwrap_flag_info external_resources_access_flag_info[] = {
	FLAG_INFO(MALI_EXT_RES_ACCESS_SHARED),
	FLAG_INFO(MALI_EXT_RES_ACCESS_EXCLUSIVE),
	{}
};

static const struct panwrap_flag_info mali_jd_dep_type_flag_info[] = {
	FLAG_INFO(MALI_JD_DEP_TYPE_DATA),
	FLAG_INFO(MALI_JD_DEP_TYPE_ORDER),
	{}
};
#undef FLAG_INFO

static struct mapped_memory *find_mapped_mem(void *addr)
{
	struct mapped_memory *pos = NULL;

	list_for_each_entry(pos, &mmaps, node) {
		if (pos->addr == addr)
			return pos;
	}

	return NULL;
}

static struct mapped_memory *find_mapped_mem_containing(void *addr)
{
	struct mapped_memory *pos = NULL;

	list_for_each_entry(pos, &mmaps, node) {
		if (addr >= pos->addr && addr <= pos->addr + pos->length)
			return pos;
	}

	return NULL;
}

static struct mapped_memory *find_gpu_mapped_mem(uint64_t addr)
{
	struct mapped_memory *pos = NULL;

	list_for_each_entry(pos, &mmaps, node) {
		if (addr >= pos->gpu_va && addr <= pos->gpu_va + pos->length)
			return pos;
	}

	return NULL;
}

void *fetch_mapped_gpu(u64 gpu_addr, size_t sz)
{
	struct mapped_memory *mem;

	if (!gpu_addr) {
		panwrap_log("Tried to dereference GPU null!\n");
		return NULL;
	}

	mem = find_gpu_mapped_mem(gpu_addr);

	if (!mem) {
		panwrap_log("Unmapped GPU mem %" PRIx64 "\n", gpu_addr);

		return NULL;
	}

	/* Sanity bounds check */
	if (gpu_addr - mem->gpu_va + sz > mem->length) {
		panwrap_log("GPU memory overflow @ %" PRIx64 "\n", gpu_addr);
		return NULL;
	}

	return mem->addr + (gpu_addr - mem->gpu_va);
}

static inline const char *
ioctl_decode_coherency_mode(enum mali_ioctl_coherency_mode mode)
{
	switch (mode) {
	case COHERENCY_ACE_LITE: return "ACE_LITE";
	case COHERENCY_ACE:      return "ACE";
	case COHERENCY_NONE:     return "None";
	default:                 return "???";
	}
}

static inline const char *
ioctl_decode_jd_prio(mali_jd_prio prio)
{
	switch (prio) {
	case MALI_JD_PRIO_LOW:    return "Low";
	case MALI_JD_PRIO_MEDIUM: return "Medium";
	case MALI_JD_PRIO_HIGH:   return "High";
	default:                  return "???";
	}
}

/*
 * Decodes the jd_core_req flags and their real meanings
 * See mali_kbase_jd.c
 */
static inline const char *
ioctl_get_job_type_from_jd_core_req(mali_jd_core_req req)
{
	if (req & MALI_JD_REQ_SOFT_JOB)
		return "Soft job";
	if (req & MALI_JD_REQ_ONLY_COMPUTE)
		return "Compute Shader Job";

	switch (req & (MALI_JD_REQ_FS | MALI_JD_REQ_CS | MALI_JD_REQ_T)) {
	case MALI_JD_REQ_DEP:
		return "Dependency only job";
	case MALI_JD_REQ_FS:
		return "Fragment shader job";
	case MALI_JD_REQ_CS:
		return "Vertex/Geometry shader job";
	case MALI_JD_REQ_T:
		return "Tiler job";
	case (MALI_JD_REQ_FS | MALI_JD_REQ_CS):
		return "Fragment shader + vertex/geometry shader job";
	case (MALI_JD_REQ_FS | MALI_JD_REQ_T):
		return "Fragment shader + tiler job";
	case (MALI_JD_REQ_CS | MALI_JD_REQ_T):
		return "Vertex/geometry shader job + tiler job";
	case (MALI_JD_REQ_FS | MALI_JD_REQ_CS | MALI_JD_REQ_T):
		return "Fragment shader + vertex/geometry shader job + tiler job";
	}

	return "???";
}

#define SOFT_FLAG(flag)                                  \
	case MALI_JD_REQ_SOFT_##flag:                    \
		panwrap_log_cont("%s)", "SOFT_" #flag); \
		break
/* Decodes the actual jd_core_req flags, but not their meanings */
static inline void
ioctl_log_decoded_jd_core_req(mali_jd_core_req req)
{
	if (req & MALI_JD_REQ_SOFT_JOB) {
		panwrap_log_cont("0x%010x (", req);

		switch (req) {
		SOFT_FLAG(DUMP_CPU_GPU_TIME);
		SOFT_FLAG(FENCE_TRIGGER);
		SOFT_FLAG(FENCE_WAIT);
		SOFT_FLAG(REPLAY);
		SOFT_FLAG(EVENT_WAIT);
		SOFT_FLAG(EVENT_SET);
		SOFT_FLAG(EVENT_RESET);
		SOFT_FLAG(DEBUG_COPY);
		SOFT_FLAG(JIT_ALLOC);
		SOFT_FLAG(JIT_FREE);
		SOFT_FLAG(EXT_RES_MAP);
		SOFT_FLAG(EXT_RES_UNMAP);
		default: panwrap_log_cont("???" ")"); break;
		}
	} else {
		panwrap_log_decoded_flags(jd_req_flag_info, req);
	}
}
#undef SOFT_FLAG

static void
ioctl_decode_pre_mem_alloc(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_alloc *args = ptr;

	panwrap_log("\tva_pages = %" PRId64 "\n", args->va_pages);
	panwrap_log("\tcommit_pages = %" PRId64 "\n", args->commit_pages);
	panwrap_log("\textent = 0x%" PRIx64 "\n", args->extent);

	panwrap_log("\tflags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
}

static void
ioctl_decode_pre_mem_import(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_import *args = ptr;
	const char *type;

	switch (args->type) {
	case MALI_MEM_IMPORT_TYPE_UMP:         type = "UMP"; break;
	case MALI_MEM_IMPORT_TYPE_UMM:         type = "UMM"; break;
	case MALI_MEM_IMPORT_TYPE_USER_BUFFER: type = "User buffer"; break;
	default:                               type = "Invalid"; break;
	}

	panwrap_log("\tphandle = 0x%" PRIx64 "\n", args->phandle);
	panwrap_log("\ttype = %d (%s)\n", args->type, type);

	panwrap_log("\tflags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
}

static void
ioctl_decode_pre_mem_commit(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_commit *args = ptr;

	panwrap_log("\tgpu_addr = 0x%" PRIx64 "\n", args->gpu_addr);
	panwrap_log("\tpages = %" PRId64 "\n", args->pages);
}

static void
ioctl_decode_pre_mem_query(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_query *args = ptr;
	char *query_name;

	switch (args->query) {
	case MALI_MEM_QUERY_COMMIT_SIZE: query_name = "Commit size"; break;
	case MALI_MEM_QUERY_VA_SIZE:     query_name = "VA size"; break;
	case MALI_MEM_QUERY_FLAGS:       query_name = "Flags"; break;
	default:                         query_name = "???"; break;
	}

	panwrap_log("\tgpu_addr = 0x%" PRIx64 "\n", args->gpu_addr);
	panwrap_log("\tquery = %d (%s)\n", args->query, query_name);
}

static void
ioctl_decode_pre_mem_free(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_free *args = ptr;

	panwrap_log("\tgpu_addr = 0x%" PRIx64 "\n", args->gpu_addr);
}

static void
ioctl_decode_pre_mem_flags_change(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_flags_change *args = ptr;

	panwrap_log("\tgpu_va = 0x%" PRIx64 "\n", args->gpu_va);
	panwrap_log("\tflags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
	panwrap_log("\tmask = 0x%" PRIx64 "\n", args->mask);
}

static void
ioctl_decode_pre_mem_alias(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_alias *args = ptr;

	panwrap_log("\tflags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
	panwrap_log("\tstride = %" PRId64 "\n", args->stride);
	panwrap_log("\tnents = %" PRId64 "\n", args->nents);
	panwrap_log("\tai = 0x%" PRIx64 "\n", args->ai);
}

static inline void
ioctl_decode_pre_sync(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_sync *args = ptr;
	const char *type;
	struct mapped_memory *mem = find_mapped_mem((void*) (uintptr_t) args->handle);

	switch (args->type) {
	case MALI_SYNC_TO_DEVICE: type = "device <- CPU"; break;
	case MALI_SYNC_TO_CPU:    type = "device -> CPU"; break;
	default:                  type = "???"; break;
	}

	if (mem) {
		panwrap_log("\thandle = %p (end=%p, len=%zu)\n",
			    (void*) (uintptr_t) args->handle,
			    (void*) (uintptr_t) args->handle + mem->length,
			    mem->length);
		panwrap_log("\tuser_addr = %p - %p (offset=%" PRIu64 ")\n",
			    args->user_addr, args->user_addr + args->size,
			    (u64) (uintptr_t) args->user_addr - (u64) (uintptr_t) args->handle);
	} else {
		panwrap_log("\tERROR! Unknown handle specified\n");
		panwrap_log("\thandle = 0x%p\n", (void*) (uintptr_t) args->handle);
		panwrap_log("\tuser_addr = %p - %p\n",
			    args->user_addr, args->user_addr + args->size);
	}
	panwrap_log("\tsize = %" PRId64 "\n", args->size);
	panwrap_log("\ttype = %d (%s)\n", args->type, type);

	if (args->type == MALI_SYNC_TO_DEVICE) {
		panwrap_log("\tDumping memory being synced to device:\n");
		panwrap_log_hexdump(args->user_addr, args->size, "\t\t");
	}
}

static void
ioctl_decode_pre_set_flags(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_set_flags *args = ptr;

	panwrap_log("\tcreate_flags = %08x\n", args->create_flags);
}

static inline void
ioctl_decode_pre_stream_create(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_stream_create *args = ptr;

	panwrap_log("\tname = %s\n", args->name);
}

/* TODO: Decode offline */
#define __PANWRAP
#include <pantrace.h>

static inline void
ioctl_decode_pre_job_submit(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_job_submit *args = ptr;
	const struct mali_jd_atom_v2 *atoms = args->addr;

	panwrap_log("\taddr = %p\n", args->addr);
	panwrap_log("\tnr_atoms = %d\n", args->nr_atoms);
	panwrap_log("\tstride = %d\n", args->stride);

	/* The stride should be equivalent to the length of the structure,
	 * if it isn't then it's possible we're somehow tracing one of the
	 * legacy job formats
	 */
	if (args->stride != sizeof(*atoms)) {
		panwrap_log("\tSIZE MISMATCH (stride should be %zd, was %d)\n",
			    sizeof(*atoms), args->stride);
		panwrap_log("\tCannot dump atoms :(, maybe it's a legacy job format?\n");
		return;
	}

	panwrap_log("\tAtoms:\n");
	for (unsigned int i = 0; i < args->nr_atoms; i++) {
		const struct mali_jd_atom_v2 *a = &atoms[i];
		struct mapped_memory *mem;

		panwrap_log("\t\tjc = 0x%" PRIx64 "\n", a->jc);
		mem = find_mapped_mem_containing((void*) (uintptr_t) a->jc);
		if (mem) {
			off_t offset = (void*) (uintptr_t) a->jc - mem->addr;

			panwrap_log("\t\tAddress %" PRIu64 " bytes inside mmap %p - %p (length=%zd)\n",
				    (loff_t) offset, mem->addr, mem->addr + mem->length,
				    mem->length);

			chai_trace_atom(a);

		} else {
			panwrap_log("\t\tERROR! jc contained in unknown memory region, cannot dump\n");
		}

		panwrap_log("\t\tudata = [0x%" PRIx64 ", 0x%" PRIx64 "]\n",
			    a->udata.blob[0], a->udata.blob[1]);
		panwrap_log("\t\tnr_ext_res = %d\n", a->nr_ext_res);

		if (a->ext_res_list) {
			panwrap_log("\t\text_res_list.count = %" PRId64 "\n",
				    a->ext_res_list->count);
			panwrap_log("\t\tExternal resources:\n");

			for (unsigned int j = 0; j < a->nr_ext_res; j++)
			{
				panwrap_log("\t\t\t");
				panwrap_log_decoded_flags(
					external_resources_access_flag_info,
					a->ext_res_list[j].ext_resource[0]);
				panwrap_log_cont("\n");
			}
		} else {
			panwrap_log("\t\t<no external resources>\n");
		}

		panwrap_log("\t\tcompat_core_req = 0x%x\n", a->compat_core_req);

		panwrap_log("\t\tPre-dependencies:\n");
		for (unsigned int j = 0; j < ARRAY_SIZE(a->pre_dep); j++) {
			panwrap_log("\t\t\tatom_id = %d flags == ",
				    a->pre_dep[i].atom_id);
			panwrap_log_decoded_flags(
			    mali_jd_dep_type_flag_info,
			    a->pre_dep[i].dependency_type);
			panwrap_log_cont("\n");
		}

		panwrap_log("\t\tatom_number = %d\n", a->atom_number);
		panwrap_log("\t\tprio = %d (%s)\n",
			    a->prio, ioctl_decode_jd_prio(a->prio));
		panwrap_log("\t\tdevice_nr = %d\n", a->device_nr);

		panwrap_log("\t\tJob type = %s\n",
			    ioctl_get_job_type_from_jd_core_req(a->core_req));
		panwrap_log("\t\tcore_req = ");
		ioctl_log_decoded_jd_core_req(a->core_req);
		panwrap_log_cont("\n");
	}
}

static void
ioctl_decode_pre(unsigned long int request, void *ptr)
{
	switch (IOCTL_CASE(request)) {
	case IOCTL_CASE(MALI_IOCTL_MEM_ALLOC):
		ioctl_decode_pre_mem_alloc(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_IMPORT):
		ioctl_decode_pre_mem_import(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_COMMIT):
		ioctl_decode_pre_mem_commit(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_QUERY):
		ioctl_decode_pre_mem_query(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_FREE):
		ioctl_decode_pre_mem_free(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_FLAGS_CHANGE):
		ioctl_decode_pre_mem_flags_change(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_ALIAS):
		ioctl_decode_pre_mem_alias(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_SYNC):
		ioctl_decode_pre_sync(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_SET_FLAGS):
		ioctl_decode_pre_set_flags(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_STREAM_CREATE):
		ioctl_decode_pre_stream_create(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_JOB_SUBMIT):
		ioctl_decode_pre_job_submit(request, ptr);
		break;
	default:
		break;
	}
}

static void
ioctl_decode_post_get_version(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_get_version *args = ptr;

	panwrap_log("\tmajor = %3d\n", args->major);
	panwrap_log("\tminor = %3d\n", args->minor);
}

static void
ioctl_decode_post_mem_alloc(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_alloc *args = ptr;
	struct allocated_memory *new = malloc(sizeof(*new));

	panwrap_log("\tgpu_va = 0x%" PRIx64 "\n", args->gpu_va);
	panwrap_log("\tva_alignment = %d\n", args->va_alignment);

	panwrap_log("\tflags = %" PRIx64, args->flags);

	new->gpu_va = args->gpu_va;
	new->same_va = args->flags & MALI_MEM_SAME_VA;
	list_add(&new->node, &allocations);

	panwrap_log_cont("\n");
}

static void
ioctl_decode_post_mem_import(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_import *args = ptr;

	panwrap_log("\tgpu_va = 0x%" PRIx64 "\n", args->gpu_va);
	panwrap_log("\tva_pages = %" PRId64 "\n", args->va_pages);
	panwrap_log("\tflags = ");
	panwrap_log_decoded_flags(mem_flag_info, args->flags);
	panwrap_log_cont("\n");
}

static void
ioctl_decode_post_mem_commit(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_commit *args = ptr;

	panwrap_log("\tresult_subcode = %d\n", args->result_subcode);

	struct allocated_memory *new = malloc(sizeof(*new));
	new->gpu_va = args->gpu_addr;
	new->same_va = false;
	list_add(&new->node, &allocations);
}

static void
ioctl_decode_post_mem_query(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_query *args = ptr;

	panwrap_log("\tvalue = 0x%" PRIx64 "\n", args->value);
}

static void
ioctl_decode_post_mem_alias(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_mem_alias *args = ptr;

	panwrap_log("\tgpu_va = 0x%" PRIx64 "\n", args->gpu_va);
	panwrap_log("\tva_pages = %" PRId64 "\n", args->va_pages);
}

static inline void
ioctl_decode_post_sync(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_sync *args = ptr;

	if (args->type != MALI_SYNC_TO_CPU)
		return;

	panwrap_log("\tDumping memory from device:\n");
	panwrap_log_hexdump(args->user_addr, args->size, "\t\t");
}

static void
ioctl_decode_post_gpu_props_reg_dump(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_gpu_props_reg_dump *args = ptr;
	const char *implementation;

	switch (args->thread.impl_tech) {
	case MALI_GPU_IMPLEMENTATION_UNKNOWN: implementation = "Unknown"; break;
	case MALI_GPU_IMPLEMENTATION_SILICON: implementation = "Silicon"; break;
	case MALI_GPU_IMPLEMENTATION_FPGA:    implementation = "FPGA"; break;
	case MALI_GPU_IMPLEMENTATION_SW:      implementation = "Software"; break;
	}

	panwrap_log("\tcore:\n");
	panwrap_log("\t\tProduct ID: %d\n", args->core.product_id);
	panwrap_log("\t\tVersion status: %d\n", args->core.version_status);
	panwrap_log("\t\tMinor revision: %d\n", args->core.minor_revision);
	panwrap_log("\t\tMajor revision: %d\n", args->core.major_revision);
	panwrap_log("\t\tGPU speed (?): %dMHz\n", args->core.gpu_speed_mhz);
	panwrap_log("\t\tGPU frequencies (?): %dKHz-%dKHz\n",
		    args->core.gpu_freq_khz_min, args->core.gpu_freq_khz_max);
	panwrap_log("\t\tShader program counter size: %.lf MB\n",
		    pow(2, args->core.log2_program_counter_size) / 1024 / 1024);

	panwrap_log("\t\tTexture features:\n");
	for (unsigned int i = 0; i < ARRAY_SIZE(args->core.texture_features); i++)
		panwrap_log("\t\t\t%010x\n", args->core.texture_features[i]);

	panwrap_log("\t\tAvailable memory: %" PRId64 " bytes\n",
		    args->core.gpu_available_memory_size);

	panwrap_log("\tL2 cache:\n");
	panwrap_log("\t\tLine size: %.lf (bytes, words?)\n",
		    pow(2, args->l2.log2_line_size));
	panwrap_log("\t\tCache size: %.lf KB\n",
		    pow(2, args->l2.log2_cache_size) / 1024);
	panwrap_log("\t\tL2 slice count: %d\n", args->l2.num_l2_slices);

	panwrap_log("\tTiler:\n");
	panwrap_log("\t\tBinary size: %d bytes\n",
		    args->tiler.bin_size_bytes);
	panwrap_log("\t\tMax active levels: %d\n",
		    args->tiler.max_active_levels);

	panwrap_log("\tThreads:\n");
	panwrap_log("\t\tMax threads: %d\n", args->thread.max_threads);
	panwrap_log("\t\tMax threads per workgroup: %d\n",
		    args->thread.max_workgroup_size);
	panwrap_log("\t\tMax threads allowed for synchronizing on simple barrier: %d\n",
		    args->thread.max_barrier_size);
	panwrap_log("\t\tMax registers available per-core: %d\n",
		    args->thread.max_registers);
	panwrap_log("\t\tMax tasks that can be sent to a core before blocking: %d\n",
		    args->thread.max_task_queue);
	panwrap_log("\t\tMax allowed thread group split value: %d\n",
		    args->thread.max_thread_group_split);
	panwrap_log("\t\tImplementation type: %d (%s)\n",
		    args->thread.impl_tech, implementation);

	panwrap_log("\tRaw props:\n");
	panwrap_log("\t\tShader present? %s\n", YES_NO(args->raw.shader_present));
	panwrap_log("\t\tTiler present? %s\n", YES_NO(args->raw.tiler_present));
	panwrap_log("\t\tL2 present? %s\n", YES_NO(args->raw.l2_present));
	panwrap_log("\t\tStack present? %s\n", YES_NO(args->raw.stack_present));
	panwrap_log("\t\tL2 features: 0x%010x\n", args->raw.l2_features);
	panwrap_log("\t\tSuspend size: %d\n", args->raw.suspend_size);
	panwrap_log("\t\tMemory features: 0x%010x\n", args->raw.mem_features);
	panwrap_log("\t\tMMU features: 0x%010x\n", args->raw.mmu_features);
	panwrap_log("\t\tAS (what is this?) present? %s\n",
		    YES_NO(args->raw.as_present));

	panwrap_log("\t\tJS (what is this?) present? %s\n",
		    YES_NO(args->raw.js_present));
	panwrap_log("\t\tJS features:\n");
	for (unsigned int i = 0; i < ARRAY_SIZE(args->raw.js_features); i++)
		panwrap_log("\t\t\t%010x\n", args->raw.js_features[i]);

	panwrap_log("\t\tTiler features: %010x\n", args->raw.tiler_features);

	panwrap_log("\t\tGPU ID: 0x%x\n", args->raw.gpu_id);
	panwrap_log("\t\tThread features: 0x%x\n", args->raw.thread_features);
	panwrap_log("\t\tCoherency mode: 0x%x (%s)\n",
		    args->raw.coherency_mode,
		    ioctl_decode_coherency_mode(args->raw.coherency_mode));

	panwrap_log("\tCoherency info:\n");
	panwrap_log("\t\tNumber of groups: %d\n", args->coherency_info.num_groups);
	panwrap_log("\t\tNumber of core groups (coherent or not): %d\n",
		    args->coherency_info.num_core_groups);
	panwrap_log("\t\tFeatures: 0x%x\n", args->coherency_info.coherency);
	panwrap_log("\t\tGroups:\n");
	for (unsigned int i = 0; i < args->coherency_info.num_groups; i++) {
		panwrap_log("\t\t\t- Core mask: %010" PRIx64 "\n",
			    args->coherency_info.group[i].core_mask);
		panwrap_log("\t\t\t  Number of cores: %d\n",
			    args->coherency_info.group[i].num_cores);
	}
}

static inline void
ioctl_decode_post_stream_create(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_stream_create *args = ptr;

	panwrap_log("\tfd = %d\n", args->fd);
}

static inline void
ioctl_decode_post_get_context_id(unsigned long int request, void *ptr)
{
	const struct mali_ioctl_get_context_id *args = ptr;

	panwrap_log("\tid = 0x%" PRIx64 "\n", args->id);
}

static void
ioctl_decode_post(unsigned long int request, void *ptr)
{
	switch (IOCTL_CASE(request)) {
	case IOCTL_CASE(MALI_IOCTL_GET_VERSION):
	case IOCTL_CASE(MALI_IOCTL_GET_VERSION_NEW):
		ioctl_decode_post_get_version(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_ALLOC):
		ioctl_decode_post_mem_alloc(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_IMPORT):
		ioctl_decode_post_mem_import(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_COMMIT):
		ioctl_decode_post_mem_commit(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_QUERY):
		ioctl_decode_post_mem_query(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_MEM_ALIAS):
		ioctl_decode_post_mem_alias(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_SYNC):
		ioctl_decode_post_sync(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_GPU_PROPS_REG_DUMP):
		ioctl_decode_post_gpu_props_reg_dump(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_STREAM_CREATE):
		ioctl_decode_post_stream_create(request, ptr);
		break;
	case IOCTL_CASE(MALI_IOCTL_GET_CONTEXT_ID):
		ioctl_decode_post_get_context_id(request, ptr);
		break;
	default:
		break;
	}
}

/**
 * Overriden libc functions start here
 */
int
panwrap_open_wrap(open_func *orig_open, const char *path, int flags, va_list args)
{
	mode_t mode = 0;
	int ret;

	if (flags & O_CREAT) {
		mode = (mode_t) va_arg(args, int);
		ret = orig_open(path, flags, mode);
	} else {
		ret = orig_open(path, flags);
	}

	LOCK();
	if (ret != -1) {
		if (strcmp(path, "/dev/mali0") == 0) {
			panwrap_log("/dev/mali0 fd == %d\n", ret);
			mali_fd = ret;
		} else if (strstr(path, "/dev/")) {
			panwrap_log("Unknown device %s opened at fd %d\n",
				    path, ret);
		}
	}
	UNLOCK();

	return ret;
}

int
open(const char *path, int flags, ...)
{
	PROLOG(open);
	va_list args;
	va_start(args, flags);
	int o = panwrap_open_wrap(orig_open, path, flags, args);
	va_end(args);
	return o;
}

int
open64(const char *path, int flags, ...)
{
	PROLOG(open64);
	va_list args;
	va_start(args, flags);
	int o = panwrap_open_wrap(orig_open64, path, flags, args);
	va_end(args);
	return o;
}


int
close(int fd)
{
	PROLOG(close);

	LOCK();
	if (fd > 0 && fd == mali_fd) {
		panwrap_log("/dev/mali0 closed\n");
		mali_fd = 0;
	}
	UNLOCK();

	return orig_close(fd);
}

/* XXX: Android has a messed up ioctl signature */
int ioctl(int fd, unsigned long request, ...)
{
	const char *name;
	union mali_ioctl_header *header;
	PROLOG(ioctl);
	int ioc_size = _IOC_SIZE(request);
	int ret;
	uint32_t func;
	void *ptr;

	if (ioc_size) {
		va_list args;

		va_start(args, request);
		ptr = va_arg(args, void *);
		va_end(args);
	} else {
		ptr = NULL;
	}

	if (fd && fd != mali_fd)
		return orig_ioctl(fd, request, ptr);

	LOCK();
	panwrap_freeze_time();
	header = ptr;

	name = ioctl_get_info(request)->name;
	if (!name)
		name = "???";

	if (!ptr) { /* All valid mali ioctl's should have a specified arg */
		panwrap_log("<%-20s> (%02lu) (%08lx), has no arguments? Cannot decode :(\n",
			    name, _IOC_NR(request), request);

		panwrap_unfreeze_time();
		ret = orig_ioctl(fd, request, ptr);
		panwrap_freeze_time();

		panwrap_log("\t= %02d\n", ret);
		goto out;
	}

	func = header->id;
	panwrap_log("<%-20s> (%02lu) (%08lx) (%04lu) (%03d)\n",
		    name, _IOC_NR(request), request, _IOC_SIZE(request), func);
	ioctl_decode_pre(request, ptr);

	panwrap_unfreeze_time();
	ret = orig_ioctl(fd, request, ptr);
	panwrap_freeze_time();

	panwrap_log("\t= %02d, %02d\n",
		    ret, header->rc);
	ioctl_decode_post(request, ptr);

out:
	panwrap_unfreeze_time();
	UNLOCK();
	return ret;
}

static inline void *panwrap_mmap_wrap(mmap_func *func,
				      void *addr, size_t length, int prot,
				      int flags, int fd, loff_t offset)
{
	struct allocated_memory *pos = NULL;
	struct mapped_memory *new;
	void *ret;
	bool found = false;
	bool same_va = false;

	if (!mali_fd || fd != mali_fd)
		return func(addr, length, prot, flags, fd, offset);

	LOCK();
	ret = func(addr, length, prot, flags, fd, offset);

	panwrap_freeze_time();

	new = calloc(sizeof(*new), 1);
	new->length = length;
	new->addr = ret;

	list_for_each_entry(pos, &allocations, node) {
		/* The kernel driver uses the offset to specify which GPU VA
		 * we're mapping */
		if (pos->gpu_va == (u64) offset) {
			found = true;
			same_va = pos->same_va;
			list_del(&pos->node);
			free(pos);
			break;
		}
	}

	if (found) {
		new->gpu_va = same_va ? (u64) (uintptr_t) ret : (u64) offset;
		panwrap_log("GPU memory 0x%" PRIx64 " mapped to %p - %p length=%zu\n",
			    offset, ret, ret + length, length);
	} else {
		panwrap_log("Unknown memory mapping %p - %p: offset=%" PRId64 " length=%zu prot = ",
			    ret, ret + length, offset, length);
		panwrap_log_decoded_flags(mmap_prot_flag_info, prot);
		panwrap_log_cont(" flags = ");
		panwrap_log_decoded_flags(mmap_flags_flag_info, flags);
		panwrap_log_cont("\n");
	}
	list_add(&new->node, &mmaps);
	panwrap_unfreeze_time();
	UNLOCK();
	return ret;
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd,
	     loff_t offset)
{
	PROLOG(mmap64);

	return panwrap_mmap_wrap(orig_mmap64, addr, length, prot, flags, fd,
				 offset);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
#ifdef __LP64__
	PROLOG(mmap);

	return panwrap_mmap_wrap(orig_mmap, addr, length, prot, flags, fd,
							offset);
#else
	return mmap64(addr, length, prot, flags, fd, (loff_t) offset);
#endif
}

int munmap(void *addr, size_t length)
{
	int ret;
	struct mapped_memory *mem;
	PROLOG(munmap);

	LOCK();
	ret = orig_munmap(addr, length);

	panwrap_freeze_time();

	mem = find_mapped_mem(addr);
	if (!mem)
		goto out;

	/* Was it memory mapped from the GPU? */
	if (mem->gpu_va)
		panwrap_log("Unmapped GPU memory 0x%" PRIx64 "@%p\n",
			    mem->gpu_va, mem->addr);
	else
		panwrap_log("Unmapped unknown memory %p\n",
			    mem->addr);

	list_del(&mem->node);
	free(mem);
out:
	panwrap_unfreeze_time();
	UNLOCK();
	return ret;
}
