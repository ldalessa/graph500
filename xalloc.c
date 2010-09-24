/* -*- mode: C; mode: folding; fill-column: 70; -*- */
#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/mman.h>
#if !defined(MAP_HUGETLB)
#define MAP_HUGETLB 0
#endif
#if !defined(MAP_POPULATE)
#define MAP_POPULATE 0
#endif

#include "compat.h"

void *
xmalloc (size_t sz)
{
  void *out;
  if (!(out = malloc (sz))) {
    perror ("malloc failed");
    abort ();
  }
  return out;
}

#if defined(__MTA__)||defined(USE_MMAP_LARGE)
#define MAX_LARGE 32
static int n_large_alloc = 0;
static struct {
  void * p;
  size_t sz;
  int fd;
} large_alloc[MAX_LARGE];

static int installed_handler = 0;
static void (*old_abort_handler)(int);

static void
exit_handler (void)
{
  int k;
  for (k = 0; k < n_large_alloc; ++k) {
    if (large_alloc[k].p)
      munmap (large_alloc[k].p, large_alloc[k].sz);
    if (large_alloc[k].fd >= 0)
      close (large_alloc[k].fd);
    large_alloc[k].p = NULL;
    large_alloc[k].fd = -1;
  }
}

static void
abort_handler (int passthrough)
{
  exit_handler ();
  if (old_abort_handler) old_abort_handler (passthrough);
}
#endif

void *
xmalloc_large (size_t sz)
{
#if defined(__MTA__)||defined(USE_MMAP_LARGE)
  void *out;
  int which = n_large_alloc++;
  if (n_large_alloc > MAX_LARGE) {
    fprintf (stderr, "Too many large allocations.\n");
    --n_large_alloc;
    abort ();
  }
  large_alloc[which].p = NULL;
  large_alloc[n_large_alloc].fd = -1;
  out = mmap (NULL, sz, PROT_READ|PROT_WRITE,
	      MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB|MAP_POPULATE, 0, 0);
  if (!out) {
    perror ("mmap failed");
    abort ();
  }
  large_alloc[n_large_alloc].p = out;
  large_alloc[n_large_alloc].sz = sz;
  return out;
#else
  return xmalloc (sz);
#endif
}

void
xfree_large (void *p)
{
#if defined(__MTA__)||defined(USE_MMAP_LARGE)
  int k;
  for (k = 0; k < n_large_alloc; ++k) {
    if (p == large_alloc[k].p) {
      munmap (p, large_alloc[k].sz);
      large_alloc[k].p = NULL;
      if (large_alloc[k].fd >= 0) {
	close (large_alloc[k].fd);
	large_alloc[k].fd = -1;
      }
      break;
    }
  }
  --n_large_alloc;
  for (; k < n_large_alloc; ++k)
    large_alloc[k] = large_alloc[k+1];
#else
  free (p);
#endif
}

void *
xmalloc_large_ext (size_t sz)
{
#if !defined(__MTA__)&&defined(USE_MMAP_LARGE_EXT)
  char extname[] = "/tmp/graph500-ext-XXXXXX";
  void *out;
  int fd, which;

  which = n_large_alloc++;
  if (n_large_alloc > MAX_LARGE) {
    fprintf (stderr, "Out of large allocations.\n");
    abort ();
  }
  large_alloc[which].p = 0;
  large_alloc[which].fd = -1;

  fd = mkstemp (extname);
  if (fd < 0) {
    perror ("xmalloc_large_ext failed to make a file");
    abort ();
  }
  if (unlink (extname)) {
    perror ("UNLINK FAILED!");
    goto errout;
  }

  if (lseek (fd, sz - sizeof(fd), SEEK_SET) < 0) {
    perror ("lseek failed");
    goto errout;
  }
  if (write (fd, &fd, sizeof(fd)) != sizeof (fd)) {
    perror ("resizing write failed");
    goto errout;
  }

  out = mmap (NULL, sz, PROT_READ|PROT_WRITE,
	      MAP_PRIVATE|MAP_HUGETLB|MAP_POPULATE, fd, 0);
  if (!out) {
    perror ("mmap failed");
    goto errout;
  }

  if (!installed_handler) {
    installed_handler = 1;
    if (atexit (exit_handler)) {
      perror ("failed to install exit handler");
      goto errout;
    }

    old_abort_handler = signal (SIGABRT, abort_handler);
    if (SIG_ERR == old_abort_handler) {
      perror ("failed to install cleanup handler");
      goto errout;
    }
  }

  large_alloc[which].p = out;
  large_alloc[which].sz = sz;
  large_alloc[which].fdd = fd;

  return out;

 errout:
  if (fd >= 0) close (fd);
  abort ();
#else
  return xmalloc_large (sz);
#endif
}

/*
void
mark_large_unused (void *p)
{
#if !defined(__MTA__)
  int k;
  for (k = 0; k < n_large_alloc; ++k)
    if (p == large_alloc[k].p)
      posix_madvise (large_alloc[k].p, large_alloc[k].sz, POSIX_MADV_DONTNEED);
#endif
}

void
mark_large_willuse (void *p)
{
#if !defined(__MTA__)
  int k;
  for (k = 0; k < n_large_alloc; ++k)
    if (p == large_alloc[k].p)
      posix_madvise (large_alloc[k].p, large_alloc[k].sz, POSIX_MADV_WILLNEED);
#endif
}
*/
