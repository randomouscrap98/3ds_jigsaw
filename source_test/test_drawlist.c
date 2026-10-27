#include "test.h"
#include "board.h"
#include "drawlist.h"
#include "rng.h"

#include <stdlib.h>
#include <time.h>

// Check d against the board: every piece once; pieces of different groups whose boxes
// overlap keep board order; the top ntop groups come after everything else.
// Returns texture page switches along the list.
// Page of every piece as the old fixed grid packing had it (piece / perpage)
static uint8_t * test_pages(Board * b, uint32_t perpage) {
  static uint8_t page[0x10000];
  for(uint32_t p = 0; p < b->count; p++) page[p] = p / perpage;
  return page;
}

static uint32_t check_list(DrawList * d, Board * b, float pad, uint32_t perpage, uint32_t ntop) {
  uint32_t * pos = malloc(sizeof(uint32_t) * b->count);     // group slot -> order index
  uint32_t * at = malloc(sizeof(uint32_t) * b->count);      // piece -> list index
  for(uint32_t p = 0; p < b->count; p++) at[p] = UINT32_MAX;
  for(uint32_t i = 0; i < b->ngroups; i++) pos[b->order[i]] = i;
  int once = 1;
  for(uint32_t i = 0; i < b->count; i++) {
    if(at[d->pieces[i]] != UINT32_MAX) once = 0;
    at[d->pieces[i]] = i;
  }
  CHECK(once);
  int ordered = 1, topok = 1;
  uint32_t split = b->ngroups - ntop;
  for(uint32_t p = 0; p < b->count; p++) {
    float px, py;
    board_piece_center(b, p, &px, &py);
    uint32_t gp = pos[b->pieces[p].group];
    for(uint32_t q = 0; q < b->count; q++) {
      uint32_t gq = pos[b->pieces[q].group];
      if(gq <= gp) continue;
      if(gp < split && gq >= split && at[q] < at[p]) topok = 0;
      float qx, qy;
      board_piece_center(b, q, &qx, &qy);
      // Sprites overlap -> q's group is above p's in board order, so q must come later
      if(fabsf(px - qx) < 2 * pad && fabsf(py - qy) < 2 * pad && at[q] < at[p]) ordered = 0;
    }
  }
  CHECK(ordered);
  CHECK(topok);
  uint32_t sw = 0;
  for(uint32_t i = 1; i < b->count; i++) sw += d->pieces[i] / perpage != d->pieces[i - 1] / perpage;
  free(pos);
  free(at);
  return sw;
}

// The grid must give exactly the all-pairs result
static void check_grid_same(DrawList * d, Board * b, float pad, uint32_t perpage, uint32_t ntop) {
  uint16_t * want = malloc(sizeof(uint16_t) * b->count);
  d->nogrid = 1;
  drawlist_build(d, b, pad, test_pages(b, perpage), (b->count + perpage - 1) / perpage, ntop);
  for(uint32_t i = 0; i < b->count; i++) want[i] = d->pieces[i];
  uint32_t layers = d->layers;
  d->nogrid = 0;
  drawlist_build(d, b, pad, test_pages(b, perpage), (b->count + perpage - 1) / perpage, ntop);
  int same = d->layers == layers;
  for(uint32_t i = 0; i < b->count; i++) same &= want[i] == d->pieces[i];
  CHECK(same);
  free(want);
}

static double build_ms(DrawList * d, Board * b, float pad, uint32_t perpage, int nogrid) {
  d->nogrid = nogrid;
  clock_t t = clock();
  for(int i = 0; i < 100; i++) drawlist_build(d, b, pad, test_pages(b, perpage), (b->count + perpage - 1) / perpage, 0);
  d->nogrid = 0;
  return (double)(clock() - t) * 1000 / CLOCKS_PER_SEC / 100;
}

static uint32_t random_switches(Board * b, uint32_t perpage) {
  uint32_t sw = 0, last = UINT32_MAX;
  for(uint32_t i = 0; i < b->ngroups; i++) {
    for(uint16_t p = b->groups[b->order[i]].first; p != BOARD_NONE; p = b->pieces[p].next) {
      if(last != UINT32_MAX && p / perpage != last) sw++;
      last = p / perpage;
    }
  }
  return sw;
}

void test_drawlist(void) {
  // A 500-piece-ish puzzle: 22x23, cell 34, tile 62 (as layout picks), 3 pages
  const float cell = 34, pad = 31;
  const uint32_t perpage = 16 * 16;
  Board b;
  DrawList d;
  CHECK(test_board_grid(&b, 22, 23, cell, 22 * cell * 3, 23 * cell * 3) == 0);
  CHECK(drawlist_init(&d, b.count) == 0);
  board_scatter(&b, 99, b.width * 0.165f, b.height * 0.165f, b.width * 0.67f, b.height * 0.67f);
  drawlist_build(&d, &b, pad, test_pages(&b, perpage), (b.count + perpage - 1) / perpage, 0);
  uint32_t sw = check_list(&d, &b, pad, perpage, 0);
  check_grid_same(&d, &b, pad, perpage, 0);
  check_grid_same(&d, &b, pad, perpage, 30);
  printf("  fresh scatter: %lu layers, %lu switches (board order: %lu)\n",
      (unsigned long)d.layers, (unsigned long)sw, (unsigned long)random_switches(&b, perpage));
  CHECK(sw < 30);

  // Random merges, a pile in one spot, and a few groups held on top
  Rng rng;
  rng_init(&rng, 5);
  for(int i = 0; i < 150; i++) {
    uint16_t p = rng_below(&rng, b.count);
    uint32_t c = p % b.geo->cols, r = p / b.geo->cols;
    uint16_t nb = c + 1 < b.geo->cols ? p + 1 : (r + 1 < b.geo->rows ? p + b.geo->cols : p);
    BoardGroup * g = b.groups + b.pieces[p].group, * h = b.groups + b.pieces[nb].group;
    if(g == h) continue;
    h->x = g->x; h->y = g->y; h->rot = g->rot;
    board_drop(&b, b.pieces[nb].group, 1);
    board_raise(&b, b.pieces[nb].group);
  }
  for(int i = 0; i < 40; i++) {
    uint16_t gid = b.order[rng_below(&rng, b.ngroups)];
    board_move(&b, gid, b.width / 2 - b.groups[gid].x + rng_range(&rng, -40, 40),
        b.height / 2 - b.groups[gid].y + rng_range(&rng, -40, 40));
    board_raise(&b, gid);
  }
  for(uint32_t ntop = 0; ntop <= 5; ntop += 5) {
    drawlist_build(&d, &b, pad, test_pages(&b, perpage), (b.count + perpage - 1) / perpage, ntop);
    sw = check_list(&d, &b, pad, perpage, ntop);
    check_grid_same(&d, &b, pad, perpage, ntop);
    printf("  merged + pile, %lu on top: %lu groups, %lu layers, %lu switches (board order: %lu)\n",
        (unsigned long)ntop, (unsigned long)b.ngroups, (unsigned long)d.layers, (unsigned long)sw,
        (unsigned long)random_switches(&b, perpage));
  }

  printf("  merged + pile build: grid %.3f ms, all pairs %.3f ms (host)\n",
      build_ms(&d, &b, pad, perpage, 0), build_ms(&d, &b, pad, perpage, 1));
  // Pieces hanging off the table edges, and everything in one heap
  for(uint32_t i = 0; i < b.ngroups; i++) {
    uint16_t gid = b.order[i];
    board_move(&b, gid, (i % 2 ? -1 : 1) * b.width, (i % 3 ? -0.5f : 1.5f) * b.height);
  }
  check_grid_same(&d, &b, pad, perpage, 3);
  for(uint32_t i = 0; i < b.ngroups; i++) {
    uint16_t gid = b.order[i];
    board_move(&b, gid, b.width / 2 - b.groups[gid].x, b.height / 2 - b.groups[gid].y);
  }
  check_grid_same(&d, &b, pad, perpage, 0);
  // Timing (host only; the 3DS is far slower): fresh 506 pieces
  board_scatter(&b, 7, 0, 0, b.width, b.height);
  check_grid_same(&d, &b, pad, perpage, 0);
  printf("  fresh build, %lu groups: grid %.3f ms, all pairs %.3f ms (host)\n", (unsigned long)b.ngroups,
      build_ms(&d, &b, pad, perpage, 0), build_ms(&d, &b, pad, perpage, 1));
  drawlist_free(&d);
  test_board_free(&b);
}
