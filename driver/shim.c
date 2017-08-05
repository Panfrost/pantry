/* Raw shim communication appears here.
 * If/when the shim is replaced by something less hacky,
 * only this file will change.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <pandriver.h>

#ifdef __PANWRAP
int ioctl(int fd, int request, ...);
#else
#include <sys/ioctl.h>
#endif

#define m_ioctl(fd, data, ioc) \
	data.header.id = ((_IOC_TYPE(ioc) & 0xF) << 8) | _IOC_NR(ioc); \
	if(ioctl(fd, ioc, &data)) { \
		printf("Bad ioctl %d (%s)\n", ioc, strerror(errno)); \
		exit(1); \
	}

int open_kernel_module()
{
	int fd = open("/dev/mali0", O_RDWR | O_CLOEXEC);

	if(fd == -1) {
		printf("Failed to open /dev/mali0\n");
		return 1;
	}

	/* Declare the ABI version (handshake 1/3) */

	struct mali_ioctl_get_version check = {
		.major = /* MALI_UK_VERSION_MAJOR */ 0x8,
		.minor = /* MALI_UK_VERSION_MINOR */ 0x4,
	};

	m_ioctl(fd, check, MALI_IOCTL_GET_VERSION);

	/* Map the Memmap Tracking Handle (handshake 2/3) */

	uint8_t *mtp = mmap(NULL, PAGE_SIZE, PROT_NONE, MAP_SHARED, fd,
				MALI_MEM_MAP_TRACKING_HANDLE);

	if(mtp == MAP_FAILED) {
		printf("MP map failed (%s)\n", strerror(errno));
		return -1;
	}

	/* Declare special flags (handshake 3/3) */

	struct mali_ioctl_set_flags flags = {
		.create_flags = MALI_CONTEXT_CREATE_FLAG_NONE
	};

	m_ioctl(fd, flags, MALI_IOCTL_SET_FLAGS);

	return fd;
}

uint64_t alloc_gpu_pages(int fd, int pages, int e_flags)
{
	struct mali_ioctl_mem_alloc alloc = {
		.va_pages = pages,
		.commit_pages = pages,
		.extent = 0,
		.flags = e_flags
	};

	printf("Allocing %d pages flag %X to %d\n", pages, e_flags, fd);

	m_ioctl(fd, alloc, MALI_IOCTL_MEM_ALLOC);

	// return alloc.gpu_va;

	/* Only necessary when we report old versions */

	if(e_flags & MALI_MEM_SAME_VA)  {
		return (uint32_t) mmap64(NULL, pages << PAGE_SHIFT, PROT_READ | PROT_WRITE, MAP_SHARED, fd, alloc.gpu_va);
	} else {
		return alloc.gpu_va;
	}
}

uint64_t alloc_gpu_heap(int fd, int pages)
{
	struct mali_ioctl_mem_alloc alloc = {
		.va_pages = pages,
		.commit_pages = 1,
		.extent = 0x80,
		.flags = 0x26F
	};

	m_ioctl(fd, alloc, MALI_IOCTL_MEM_ALLOC);

	return alloc.gpu_va;
}

void free_gpu(int fd, uint64_t addr)
{
	struct mali_ioctl_mem_free gfree = { .gpu_addr = addr };
	m_ioctl(fd, gfree, MALI_IOCTL_MEM_FREE);
}

void sync_gpu(int fd, uint8_t* cpu, uint64_t gpu, size_t bytes)
{
	struct mali_ioctl_sync sync = {
		.handle = gpu & PAGE_MASK,
		.user_addr = cpu - (gpu & ~PAGE_MASK),
		.size = (gpu & ~PAGE_MASK) + bytes,
		.type = MALI_SYNC_TO_DEVICE
	};

	m_ioctl(fd, sync, MALI_IOCTL_SYNC);
}

static void submit_job_internal(int fd, struct mali_jd_atom_v2 *atoms, size_t count)
{
	struct mali_ioctl_job_submit submit = {
		.addr = atoms,
		.nr_atoms = count,
		.stride = sizeof(struct mali_jd_atom_v2)
	};

	m_ioctl(fd, submit, MALI_IOCTL_JOB_SUBMIT);
}

#define ATOM_QUEUE_MAX 16

struct mali_jd_atom_v2 atom_queue[ATOM_QUEUE_MAX];
int atom_queue_size = 0;

void flush_job_queue(int fd)
{
	if(atom_queue_size) {
		submit_job_internal(fd, atom_queue, atom_queue_size);
		atom_queue_size = 0;
	} else {
		printf("Warning... flushing job queue with no atoms\n");
	}
}

void submit_job(int fd, struct mali_jd_atom_v2 atom)
{
	memcpy(&atom_queue[atom_queue_size++], &atom, sizeof(atom));

	if(atom_queue_size == ATOM_QUEUE_MAX)
		flush_job_queue(fd);
}

/* Not strictly an ioctl but still shim related */

uint8_t* mmap_gpu(int fd, uint64_t addr, int page_count)
{
	uint8_t* buffer = mmap64(NULL, page_count << PAGE_SHIFT,
				PROT_READ | PROT_WRITE, MAP_SHARED,
				fd, addr);

	if(buffer == MAP_FAILED) {
		printf("Buffer map failed (%s)\n", strerror(errno));
		exit(1);
	}

	return buffer;
}

/* Seems to fail but called anyway by the blob */

void stream_create(int fd, char *stream)
{
	struct mali_ioctl_stream_create s;
	strcpy(s.name, stream);
	m_ioctl(fd, s, MALI_IOCTL_STREAM_CREATE);
}


void query_gpu_props(int fd)
{
	struct mali_ioctl_gpu_props_reg_dump v;
	m_ioctl(fd, v, MALI_IOCTL_GPU_PROPS_REG_DUMP);
}
