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
// Snakes: a short edge between a piece and a roomier neighbour becomes a finger of the piece
// (as wide as the edge, tapering) winding into the neighbour through a few points
#define STROKECUT_SNAKES 0.5f     // Share of the candidate edges that snake
#ifndef STROKECUT_SNAKEAT
#define STROKECUT_SNAKEAT 0.99f   // Snakes go in when the puzzle has this share of its pieces (sizes below
#endif                            // are in final piece sizes, scaled to the pieces then)
#define STROKECUT_SNAKEMIN 0.3f   // Candidate edges are this long ...
#define STROKECUT_SNAKEMAX 0.7f   // ... to this (cells, along the edge)
#ifndef STROKECUT_SNAKEROOM
#define STROKECUT_SNAKEROOM 1.2f  // The neighbour must be at least this much bigger
#endif
#define STROKECUT_SNAKEKEEP 0.5f  // ... and keep at least this share of its area
#define STROKECUT_SNAKEPTS 3      // Points the finger winds through (2 .. this)
#ifndef STROKECUT_SNAKESTEP
#define STROKECUT_SNAKESTEP 0.3f  // Distance between them (cells)
#endif
#ifndef STROKECUT_SNAKEMARGIN
#define STROKECUT_SNAKEMARGIN 0.05f  // Points keep half the width plus this from the neighbour's edges
#endif
#define STROKECUT_SNAKETAPER 0.2f // The finger narrows by this share toward the tip
#define STROKECUT_SNAKECLEAR 0.1f // Thinnest part of either piece afterwards (cells)
#define STROKECUT_SNAKETRIES 12
#ifndef STROKECUT_SNAKESTRAIGHT
#define STROKECUT_SNAKESTRAIGHT 0.6f  // The neighbour's boundary turns at most this much at the edge's ends
#endif

// Just the graph (polyline edges, no tabs): count pieces, then whimsies (level, set) after
// them, flagged CUTEDGE_WHIMSY. Returns 0 on success (on failure g is empty).
int strokecut_graph(CutGraph * g, float width, float height, uint32_t count, uint8_t whimsy, uint8_t set,
    uint32_t seed);

#endif
