#ifndef __HEADER_JIGSAW_STROKECUT__
#define __HEADER_JIGSAW_STROKECUT__

#include "cutgraph.h"

#include <stdint.h>

// Stroke cut (wood puzzle style): start from the whole puzzle as one region and keep cutting
// the biggest region in two with a smooth curve, ends square on to the lines they meet (T
// junctions), until there are count pieces. The first cuts cross the whole puzzle, so long
// strokes run through it. Whimsies go in first, each anchored by two strokes leaving it in
// roughly opposite directions (lines grow from them). Everything in cell units.

#define STROKECUT_STEP 0.08f      // Curve sample spacing (cells)
#define STROKECUT_CLEAR 0.15f     // A new curve keeps this far from other lines (cells)
#define STROKECUT_CORNER 0.35f    // Cut ends keep this far from corners and junctions (cells)
#define STROKECUT_TURN 0.45f      // A boundary point turning more than this (radians) is a corner
#define STROKECUT_BALANCE 0.3f    // Each half of a cut gets at least this share of the region
#define STROKECUT_TRIES 60        // Attempts per cut before trying another region
#define STROKECUT_ANCHORS 12      // Whole attempts (both strokes) to anchor a whimsy before dropping it
#define STROKECUT_SHAPE 0.25f     // Curve control points stray up to this times the cut length

// Just the graph (polyline edges, no tabs): count pieces, then whimsies (level, set) after
// them, flagged CUTEDGE_WHIMSY. Returns 0 on success (on failure g is empty).
int strokecut_graph(CutGraph * g, float width, float height, uint32_t count, uint8_t whimsy, uint8_t set,
    uint32_t seed);

#endif
