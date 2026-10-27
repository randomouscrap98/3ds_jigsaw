#ifndef __HEADER_JIGSAW_WHIMSY__
#define __HEADER_JIGSAW_WHIMSY__

#include "cutgraph.h"

#include <stdint.h>

// Whimsy pieces: shapes (hearts, stars...) cut into a puzzle across several pieces, as in
// wooden puzzles. The shape's outline becomes edges in the cut graph; the pieces it overlaps
// lose that part. Whimsies have no tabs (their shape holds them).

#define WHIMSY_NONE 0
#define WHIMSY_FEW 1
#define WHIMSY_MANY 2
#define WHIMSY_LEVELS 3

#define WHIMSY_SET_CUTE 0     // Heart, star, flower, cloud
#define WHIMSY_SETS 1

#define WHIMSY_MAXSHAPE 192   // Points in one shape outline
#ifndef WHIMSY_AREA
#define WHIMSY_AREA 1.6f      // Typical whimsy area (cells^2)
#endif
#define WHIMSY_TABEDGE 0.75f  // Edges ending at a whimsy get a tab only if at least this long
#define WHIMSY_JITTER 0.15f   // How far a whimsy center strays from the corner it is placed on
#define WHIMSY_BORDER 0.3f    // Closest a whimsy gets to the puzzle border (cells)
#define WHIMSY_GAP 0.6f       // Closest two whimsies get (cells)
#define WHIMSY_TRIES 60       // Placement attempts per whimsy per shape tried

#define WHIMSY_MEASURESTEP 0.05f  // Sampling grid for whimsy_measure (cells)
#define WHIMSY_SPIKER 0.12f       // Parts thinner than twice this count as spikes (cells)
#ifndef WHIMSY_MAXSPIKE
#define WHIMSY_MAXSPIKE 0.05f     // Most spike area a piece next to a whimsy may have (cells^2)
#endif

// How thick a polygon is (radius of the biggest circle inside), and how much of it is in
// parts thinner than 2 * WHIMSY_SPIKER (horns, slivers: area no disc of that radius
// reaches). Sampled on a WHIMSY_MEASURESTEP grid.
void whimsy_measure(const float * xy, uint32_t n, float * thick, float * spike);

// Shapes in a set, and one of them: a closed outline (clockwise, y down) of area 1 centered
// on its centroid. xy holds WHIMSY_MAXSHAPE pairs. Returns the point count.
uint32_t whimsy_setsize(uint8_t set);
uint32_t whimsy_shape(uint8_t set, uint32_t shape, float * xy);
// How many whimsies a puzzle of count pieces gets
uint32_t whimsy_count(uint8_t level, uint32_t count);
// Place whimsies into g (straight edges, tabs not given out yet except grid presets).
// New pieces are appended (their indices start at the old count). Returns the number
// placed (can be fewer than asked if no spot works), or -1 out of memory.
int whimsy_add(CutGraph * g, uint8_t level, uint8_t set, uint32_t seed);
// Insert one shape (absolute coordinates, xy n points, clockwise) as a new piece. Returns 0
// on success, 1 if it doesn't fit cleanly (g unchanged), -1 out of memory.
int whimsy_insert(CutGraph * g, const float * xy, uint32_t n);

#endif
