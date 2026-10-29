#ifndef __HEADER_JIGSAW_RANDCUT__
#define __HEADER_JIGSAW_RANDCUT__

#include "cutgraph.h"

#include <stdint.h>

// Random (non-grid) cut: a Voronoi diagram of scattered points, clipped to the puzzle
// rectangle, short edges collapsed, then tabs on the edges. Pieces have 3+ sides and
// differing sizes. Everything is in cell units (average piece area = 1 cell^2) until
// scaled for output.

// How the cut looks
typedef struct {
  uint8_t relax;      // Lloyd relaxation passes: more = more even piece sizes
  float grid;         // 0 = points scattered anywhere; else one point per grid cell, moved up
                      // to this far (cells, < 0.5) each way from its center: small = mostly
                      // four-sided pieces (count must be round(width) x rows)
  float collapse;     // Edges shorter than this (cells) are collapsed to a point
  float mintab;       // Edges shorter than this get no tab
  float twotab;       // Edges at least this long get two tabs
  float maxtab;       // Longest stretch of edge one tab is drawn over (cells)
} RandCutStyle;

#define RANDCUT_STYLE_EVEN 0
#define RANDCUT_STYLE_RECT 1
#define RANDCUT_STYLES 2
const RandCutStyle * randcut_style(uint8_t style);

#define RANDCUT_MAXTRIES 8        // Whole-cut retries (new points) before failing

// Just the graph (no tabs) for one attempt: whimsies go in before tabs. Returns 0 on success.
int randcut_graph(CutGraph * g, float width, float height, uint32_t count, uint32_t seed, const RandCutStyle * st);
// Tab rules for random cuts
void randcut_tabstyle(const RandCutStyle * st, CutTabStyle * ts);
// Whole cut with tabs: retries with new points until one works. Same seed + style = same
// cut. Free with cutgraph_free. Returns 0 on success.
int randcut_init(CutGraph * g, float width, float height, uint32_t count, uint32_t seed, const RandCutStyle * st);
// Seed for attempt t (randcut_init's retries; callers doing their own retries use the same)
uint32_t randcut_tryseed(uint32_t seed, uint32_t t);

#endif
