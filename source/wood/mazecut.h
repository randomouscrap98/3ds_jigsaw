#ifndef __HEADER_JIGSAW_MAZECUT__
#define __HEADER_JIGSAW_MAZECUT__

#include "cutgraph.h"

#include <stdint.h>

// Maze cut (experimental, not in the menu): a maze of corridors about one snake wide, its
// corridors cut into count pieces in the order the maze was dug, painted onto a fine grid,
// then every square goes to the piece that wins a blurred vote around it, so the lines only
// loosely follow the maze and come out smooth. Lines traced and smoothed by pottscut.

typedef struct {
  float corridor;     // Corridor width (cells)
  float straight;     // Chance the digging carries on straight (else any open direction)
  uint8_t res;        // Fine grid squares per cell
  float blur;         // Vote radius (cells): bigger = smoother, looser
  float warp;         // The maze is read through a smooth random displacement this big (cells),
                      // so straight corridors bend into sweeps
  uint8_t smooth;     // Smoothing passes on the traced lines
  uint8_t hex;        // Hexagonal maze (corridors turn at 60 / 120 degrees: rounder bends)
  float curl;         // Chance the digging turns the same way as last time (spirals, hooks)
  float flip;         // ... and of those, the chance it turns the other way instead (S-bends)
  float potts;        // Automaton steps on the maze pieces (proposals per square; 0 = none):
                      // grows lobes into neighbours so pieces curl into each other
  float pottsperim;   // ... target perimeter (pottscut's perim)
  float pottstemp;    // ... temperature
  float spirals;      // Double spirals stamped in, per piece (each is two pieces wound round
                      // each other, yin-yang style; the maze fills around them)
  float spiralw;      // Arm width (cells)
  float spiralarea;   // Arm area, times the average piece's
} MazeStyle;

#define MAZECUT_STYLE_DEFAULT 0
#define MAZECUT_STYLE_CURLY 1     // User marked important: ignore/maze/spiral/curl0.85_flip0.25.png
#define MAZECUT_STYLES 2
const MazeStyle * mazecut_style(uint8_t style);

#define MAZECUT_TRIES 8

// Just the graph (polyline edges, no tabs, no whimsies yet). Returns 0 on success.
int mazecut_graph(CutGraph * g, float width, float height, uint32_t count, uint32_t seed, const MazeStyle * st);

#endif
