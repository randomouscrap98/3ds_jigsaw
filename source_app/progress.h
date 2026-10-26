#ifndef __HEADER_JIGSAW_PROGRESS__
#define __HEADER_JIGSAW_PROGRESS__

// Report progress of a long blocking task (done out of total; total <= 1 = no bar)
typedef void (*progress_fn)(void * udata, const char * msg, int done, int total);

#endif
