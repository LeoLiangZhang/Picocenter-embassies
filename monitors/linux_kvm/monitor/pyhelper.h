#include <stdio.h>

struct uvmem_server_arg
{
	FILE *fp; // file pointer to saved pages
	int uvmem_fd;
	int shmem_fd;
	size_t size; // uvmem mapping sized
	size_t page_size;
	int pico_id;
};

#ifdef __cplusplus
extern "C" { 
#endif

void py_serve_uvmem_page(struct uvmem_server_arg *sarg);
long find_page_file_offset(uint8_t *vaddr);

#ifdef __cplusplus
}
#endif
