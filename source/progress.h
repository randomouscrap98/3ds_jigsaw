#ifndef __HEADER_JIGSAW_PROGRESS__
#define __HEADER_JIGSAW_PROGRESS__

#include <stddef.h>

// Report progress of a long blocking task (done out of total; total <= 1 = no bar)
typedef void (*progress_fn)(void * udata, const char * msg, int done, int total);

// A callback and its data, passed down into slow code (NULL = nobody listening)
typedef struct {
  progress_fn fn;
  void * udata;
} Progress;

static inline void progress_report(const Progress * p, const char * msg, int done, int total) {
  if(p && p->fn) p->fn(p->udata, msg, done, total);
}

#endif
