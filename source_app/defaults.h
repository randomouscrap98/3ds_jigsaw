#ifndef __HEADER_JIGSAW_DEFAULTS__
#define __HEADER_JIGSAW_DEFAULTS__

#include "progress.h"
#include "storage.h"

#define DEFAULTS_SRC    "romfs:/puzzles"
#define DEFAULTS_MARKER STORAGE_ROOT "/.defaults_installed"
#define DEFAULTS_CHUNK  (64 * 1024)
#define DEFAULTS_REDRAW (256 * 1024)  // Bytes between progress redraws (each costs a frame)

// On first launch, copy the bundled puzzles from romfs into the puzzles folder. A marker
// file records that it happened, so deleted defaults stay deleted. Existing files are
// never overwritten. Needs romfsInit() first. Returns 0 on success (or nothing to do).
int defaults_install(progress_fn cb, void * udata);

#endif
