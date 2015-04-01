#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <err.h>

#define debug_printf(format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#define PAGESIZE 4096

struct page_s
{
  char buf[PAGESIZE];
};

void profile_pages(int size)
{
  int i, j;
  size_t length = size * PAGESIZE;
  void *addr = mmap(NULL, length, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  // void *addr = malloc(length);
  if (addr == NULL)
    err(errno, "mmap fails");
  struct page_s *page = (struct page_s*)addr;
  char letter = 'a';
  while (1) {
    for (j = 0; j < PAGESIZE; j++) {
      for (i = 0; i < size; i++) {
        (page+i)->buf[j] = 'a';
      }
    }
    letter = (letter-'a'+1 % 26) + 'a';
  }
}

int main(int argc, char const *argv[], char *envp[])
{
  debug_printf("Hello world\n");
  profile_pages(1000);
  return 0;
}