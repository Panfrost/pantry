/* TODO: Find non-hackish memory allocator */

void init_cbma(int fd);
void* galloc(size_t sz);
void gfree(void* ptr);
