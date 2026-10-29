#ifndef __HEADER_JIGSAW_POTTSCUT__
#define __HEADER_JIGSAW_POTTSCUT__

#include "cutgraph.h"

#include <stdint.h>

// Cellular Potts cut (experimental, not in the menu): pieces are labels on a fine grid,
// started as even blobs, then a cellular automaton (the cellular Potts model) lets boundary
// squares change hands: each piece wants a target area and a target perimeter (longer than a
// round blob's: that makes lobes and fingers), boundaries cost a little (smooth), and a
// temperature adds noise. Pieces never split, get holes, or touch only at a corner. The
// boundaries are then traced between the points where three pieces meet and smoothed into
// curves (neighbours share them exactly). Everything in cell units.

typedef struct {
  uint8_t res;        // Grid squares per cell side
  float sweeps;       // Automaton steps, in proposals per grid square
  float temp;         // Noise
  float adhesion;     // Cost per unlike neighbour (8-neighbourhood): smoothness
  float larea;        // Weight of (area - target)^2
  float lperim;       // Weight of (perimeter - target)^2
  float perim;        // Target perimeter, times a square's of the target area
  uint8_t smooth;     // Smoothing passes on the traced lines
  float cool;         // Last share of the steps: temperature ramps down to temp * POTTSCUT_COOLTO (anneal)
} PottsStyle;

#define POTTSCUT_STYLE_DEFAULT 0
#define POTTSCUT_STYLES 1
const PottsStyle * pottscut_style(uint8_t style);

#define POTTSCUT_TRIES 8          // Whole-cut retries (new seed) before failing
#define POTTSCUT_COOLTO 0.1f      // Annealing ends at this share of the temperature
#define POTTSCUT_MINAREA 0.4f     // A piece never shrinks below this share of the target area

// Just the graph (polyline edges, no tabs): count pieces, then whimsies (level, set) after
// them, flagged CUTEDGE_WHIMSY. Same seed + style = same cut. Returns 0 on success (on failure g is empty).
int pottscut_graph(CutGraph * g, float width, float height, uint32_t count, uint8_t whimsy, uint8_t set,
    uint32_t seed, const PottsStyle * st);

#endif
