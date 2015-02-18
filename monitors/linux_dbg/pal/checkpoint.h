/*
 * liang: my library for checkpoint and resume Embassies DBG mode program.
 */

#pragma once

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int lz_tid_list[];
extern int lz_thread_count;

// Struct for parsing proc_maps output
typedef struct {
	uint32_t start;
	uint32_t size;
	int prot; // memory protection argument
	uint8_t data[0];
} lz_page_desc_s;

#define READLINE_DATA_SIZE 4096 
typedef struct {
	int fd;
	char *start;
	char *end;
	char data[READLINE_DATA_SIZE];
} lz_readline_s;

typedef struct
{
	FILE *fp;
	long page_desc_offset;
	long page_data_offset;
	lz_page_desc_s saved_page;

} lz_page_loader;

void lz_gsfree_readline_init(lz_readline_s *reader, int fd);
int lz_gsfree_readline(lz_readline_s *reader, char *line);
int lz_gsfree_write(int fp, const void *buf, int count);
unsigned int lz_strtoul(char *start, char **end, int base);
void lz_checkpoint_handler(int arg);
void lz_setup_sig_checkpoint();
void lz_checkpoint_resume();

#ifdef __cplusplus
}
#endif
