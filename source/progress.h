#ifndef __HEADER_JIGSAW_PROGRESS__
#define __HEADER_JIGSAW_PROGRESS__

#include <stddef.h>

// Report progress of a long blocking task (done out of total; total <= 1 = no bar)
typedef void (*progress_fn)(void * udata, const char * msg, int done, int total);

// Message for the cut generators' reports (an app can treat the cut stage specially)
#define PROGRESS_CUTTING "Cutting pieces"
// ... and imgscale_areap's
#define PROGRESS_SCALING "Scaling image"

// A callback and its data, passed down into slow code (NULL = nobody listening)
typedef struct {
  progress_fn fn;
  void * udata;
} Progress;

static inline void progress_report(const Progress * p, const char * msg, int done, int total) {
  if(p && p->fn) p->fn(p->udata, msg, done, total);
}

#endif
