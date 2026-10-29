#ifndef __HEADER_JIGSAW_CORNERBEND__
#define __HEADER_JIGSAW_CORNERBEND__

#include "cutgraph.h"

// Corner-paired strokes (an experiment, kept; no style uses it now): directions for stroke
// curves (CutEdge dir) so that at each inner corner the two edges closest to opposite
// continue each other smoothly (one line through the corner) and the rest meet that line
// square on (a T); edges meet the border square on. Only inner, straight, non-whimsy edges
// get directions; cutgraph_tabs then bends them by the style's bend. Returns 0 on success.
int cornerbend_dirs(CutGraph * g);

#endif
