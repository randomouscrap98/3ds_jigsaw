#ifndef __HEADER_JIGSAW_DEFAULTS__
#define __HEADER_JIGSAW_DEFAULTS__

#include "progress.h"
#include "storage.h"

#define DEFAULTS_SRC    "romfs:/puzzles"
#define DEFAULTS_MARKER STORAGE_ROOT "/.defaults_installed"
#define DEFAULTS_CHUNK  (64 * 1024)
#define DEFAULTS_REDRAW (256 * 1024)  // Bytes between progress redraws (each costs a frame)
#define DEFAULTS_VERSION 2   // Bundled pictures' version, kept in the marker (empty marker = 1).
                             // 2 (1.5): full build's paintings at 2048 px instead of 1024

// On first launch, copy the bundled puzzles from romfs into the puzzles folder. A marker
// file records that it happened (and which version), so deleted defaults stay deleted.
// When a newer version's bundle comes along, the pictures still there under their own names are
// replaced, but only by bigger ones (so the lite build, at 1024, never swaps anything) and not
// when a save would stop fitting (its piece count gives another grid with the new picture's
// shape). Needs romfsInit() first. Returns 0 on success (or nothing to do).
int defaults_install(progress_fn cb, void * udata);

#endif
