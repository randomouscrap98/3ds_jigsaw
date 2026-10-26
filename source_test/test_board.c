#include "test.h"
#include "board.h"
#include "boardsave.h"
#include "camera.h"

#include <stdlib.h>

// Put single piece p's group so the piece sits at its solved spot shifted by (ox,oy), rotated
static void place(Board * b, uint16_t p, float ox, float oy, uint8_t rot) {
  BoardGroup * g = b->groups + b->pieces[p].group;
  g->x = ox; g->y = oy; g->rot = rot;
}

void test_board(void) {
  Board b;
  CHECK(board_init(&b, 4, 3, 10, 200, 150) == 0);
  CHECK(b.count == 12 && b.ngroups == 12);

  // Solved layout: every group at 0,0 rot 0. Pick hits the right piece.
  CHECK(board_pick(&b, 15, 5) == 1);
  CHECK(board_pick(&b, 5, 25) == 8);
  CHECK(board_pick(&b, 45, 5) == -1);
  CHECK(board_pick(&b, -1, 5) == -1);

  // Pick respects rotation: piece 0 rotated once clockwise around origin covers x in [-10,0]
  place(&b, 0, 100, 100, 1);
  CHECK(board_pick(&b, 95, 105) == 0);
  CHECK(board_pick(&b, 105, 105) == -1);
  float cx, cy;
  board_piece_center(&b, 0, &cx, &cy);
  CHECKF(cx, 95); CHECKF(cy, 105);

  // Rotate four times around any pivot = identity
  for(int i = 0; i < 4; i++) board_rotate(&b, 0, 1, 33, 7);
  CHECK(b.groups[0].rot == 1);
  CHECKF(b.groups[0].x, 100); CHECKF(b.groups[0].y, 100);
  board_rotate(&b, 0, 1, 33, 7);
  board_rotate(&b, 0, -1, 33, 7);
  CHECKF(b.groups[0].x, 100); CHECKF(b.groups[0].y, 100);
  board_free(&b);

  // Rotation about a pivot keeps the pivot fixed and moves pieces correctly
  CHECK(board_init(&b, 4, 3, 10, 200, 150) == 0);
  place(&b, 5, 50, 50, 0);             // piece 5 = (1,1): center at 50+15, 50+15
  board_rotate(&b, 5, 1, 65, 65);      // spin around its own center
  board_piece_center(&b, 5, &cx, &cy);
  CHECKF(cx, 65); CHECKF(cy, 65);
  CHECK(b.groups[5].rot == 1);
  board_rotate(&b, 5, -1, 65, 65);
  CHECK(b.groups[5].rot == 0);
  CHECKF(b.groups[5].x, 50); CHECKF(b.groups[5].y, 50);

  // Merge: put 4 and 5 (neighbours) in the same frame, slightly off
  place(&b, 4, 52, 49, 0);
  CHECK(board_drop(&b, 4, 3) == 1);
  CHECK(b.pieces[5].group == 4 && b.groups[4].count == 2);
  CHECKF(b.groups[4].x, 50); CHECKF(b.groups[4].y, 50);  // snapped onto the stationary one
  CHECK(b.ngroups == 11);
  CHECK(b.groups[5].count == 0);
  // Picking either piece now yields the merged group
  CHECK(board_pick(&b, 55 + 10, 55 + 10) == 4);
  CHECK(board_pick(&b, 55, 65) == 4);

  // Not neighbours: 0 and 2 share a frame but aren't adjacent -> no merge between them
  place(&b, 0, -100, -100, 0);
  place(&b, 2, -100, -100, 0);
  CHECK(board_drop(&b, 0, 3) == 0);

  // Different rotation -> no merge
  place(&b, 6, 50, 50, 1);
  CHECK(board_drop(&b, 6, 3) == 0);
  // Too far -> no merge
  place(&b, 6, 60, 50, 0);
  CHECK(board_drop(&b, 6, 3) == 0);
  // Close enough, rotated group dropping onto rotated group
  board_rotate(&b, 4, 1, 0, 0);
  board_rotate(&b, 6, 1, 0, 0);       // both rotated same way around same pivot: still aligned
  CHECK(board_drop(&b, 6, 3) == 0);   // 6 was 10 off, rotation preserves distance
  BoardGroup g4 = b.groups[4];
  place(&b, 6, g4.x + 1, g4.y - 1, g4.rot);
  CHECK(board_drop(&b, 6, 3) == 1);
  CHECK(b.groups[6].count == 3 && b.groups[4].count == 0);
  board_free(&b);

  // Cascade: whole puzzle at the same frame merges into one in a single drop
  CHECK(board_init(&b, 5, 4, 10, 200, 150) == 0);
  CHECK(!board_complete(&b));
  CHECK(board_drop(&b, 7, 3) == 19);
  CHECK(board_complete(&b));
  CHECK(b.groups[7].count == 20);
  int n = 0;
  for(uint16_t p = b.groups[7].first; p != BOARD_NONE; p = b.pieces[p].next) n++;
  CHECK(n == 20);
  board_free(&b);

  // Raise moves a group to the top of draw order; pick prefers the top
  CHECK(board_init(&b, 2, 1, 10, 100, 100) == 0);
  place(&b, 0, 0, 0, 0);
  place(&b, 1, -10, 0, 0);   // piece 1 now overlaps piece 0's spot
  CHECK(board_pick(&b, 5, 5) == 1);
  board_raise(&b, 0);
  CHECK(b.order[1] == 0);
  CHECK(board_pick(&b, 5, 5) == 0);
  board_free(&b);

  // Scatter: everything inside the table, all separate, deterministic per seed
  CHECK(board_init(&b, 26, 19, 37, 26 * 37 * 3, 19 * 37 * 3) == 0);
  board_scatter(&b, 1234, b.width / 4, b.height / 4, b.width / 2, b.height / 2);
  CHECK(b.ngroups == b.count);
  int inside = 1, rots[4] = {0};
  for(uint32_t i = 0; i < b.count; i++) {
    board_piece_center(&b, i, &cx, &cy);
    if(cx < b.width / 4 || cy < b.height / 4 || cx > b.width * 3 / 4 || cy > b.height * 3 / 4) inside = 0;
    rots[board_piece_rot(&b, i)]++;
  }
  CHECK(inside);
  CHECK(rots[0] > 50 && rots[1] > 50 && rots[2] > 50 && rots[3] > 50);
  float x0 = b.groups[0].x;
  board_scatter(&b, 1234, b.width / 4, b.height / 4, b.width / 2, b.height / 2);
  CHECKF(b.groups[0].x, x0);
  // Nothing should merge right after a scatter (pieces are spread apart)
  int merged = 0;
  for(uint32_t i = 0; i < b.count; i++) merged += board_drop(&b, i, 37 * 0.3f);
  CHECK(merged == 0);
  board_free(&b);
}

void test_boardsave(void) {
  Board b, c;
  CHECK(board_init(&b, 6, 5, 20, 400, 300) == 0);
  board_scatter(&b, 99, 0, 0, b.width, b.height);
  // Merge a few things to make the groups interesting
  b.groups[1].x = b.groups[0].x; b.groups[1].y = b.groups[0].y; b.groups[1].rot = b.groups[0].rot;
  b.groups[7].x = b.groups[0].x; b.groups[7].y = b.groups[0].y; b.groups[7].rot = b.groups[0].rot;
  CHECK(board_drop(&b, 1, 1) == 2);
  board_raise(&b, 13);

  size_t len = boardsave_size(&b);
  uint8_t * buf = malloc(len);
  CHECK(boardsave_write(&b, buf) == len);
  BoardSaveInfo info;
  CHECK(boardsave_peek(buf, len, &info) == 0);
  CHECK(info.cols == 6 && info.rows == 5 && info.ngroups == 28);

  CHECK(board_init(&c, 6, 5, 20, 400, 300) == 0);
  CHECK(boardsave_read(&c, buf, len) == 0);
  CHECK(c.ngroups == b.ngroups);
  // Every piece ends up at the same place with the same rotation
  int same = 1;
  for(uint32_t i = 0; i < b.count; i++) {
    float x1, y1, x2, y2;
    board_piece_center(&b, i, &x1, &y1);
    board_piece_center(&c, i, &x2, &y2);
    if(fabsf(x1 - x2) > 0.001f || fabsf(y1 - y2) > 0.001f) same = 0;
    if(board_piece_rot(&b, i) != board_piece_rot(&c, i)) same = 0;
    // Grouping preserved: pieces sharing a group in b share one in c
    if((b.pieces[i].group == b.pieces[0].group) != (c.pieces[i].group == c.pieces[0].group)) same = 0;
  }
  CHECK(same);
  // Draw order preserved: top group in c holds the same pieces as top group in b
  CHECK(c.groups[c.order[c.ngroups - 1]].first == b.groups[b.order[b.ngroups - 1]].first);
  board_free(&c);

  // Cell scaling
  CHECK(board_init(&c, 6, 5, 40, 800, 600) == 0);
  CHECK(boardsave_read(&c, buf, len) == 0);
  float x1, y1, x2, y2;
  board_piece_center(&b, 3, &x1, &y1);
  board_piece_center(&c, 3, &x2, &y2);
  CHECKF(x2, x1 * 2); CHECKF(y2, y1 * 2);
  board_free(&c);

  // Rejections: wrong dims, truncation, corruption
  CHECK(board_init(&c, 5, 6, 20, 400, 300) == 0);
  CHECK(boardsave_read(&c, buf, len) != 0);
  board_free(&c);
  CHECK(board_init(&c, 6, 5, 20, 400, 300) == 0);
  float before = c.groups[0].x;
  CHECK(boardsave_read(&c, buf, len - 1) != 0);
  buf[len - 2] = 0xFF;  // group index out of range
  CHECK(boardsave_read(&c, buf, len) != 0);
  CHECKF(c.groups[0].x, before);  // untouched on failure
  buf[0] = 'X';
  CHECK(boardsave_read(&c, buf, len) != 0);
  board_free(&c);
  free(buf);
  board_free(&b);
}

void test_camera(void) {
  Camera c;
  camera_init(&c, 320, 240, 3200, 1200, 2);
  CHECKF(c.zoom, 0.1f);   // width-limited fit
  float wx, wy, sx, sy;
  camera_to_world(&c, 160, 120, &wx, &wy);
  CHECKF(wx, 1600); CHECKF(wy, 600);
  camera_zoom(&c, 0.5f);
  CHECKF(c.zoom, 0.1f);   // can't zoom out past fit
  camera_zoom(&c, 100);
  CHECKF(c.zoom, 2);
  camera_to_world(&c, 0, 0, &wx, &wy);
  camera_to_screen(&c, wx, wy, &sx, &sy);
  CHECKF(sx, 0); CHECKF(sy, 0);
  camera_pan(&c, 20, -10);
  CHECKF(c.cx, 1610); CHECKF(c.cy, 595);
  camera_pan(&c, 1e6, 1e6);
  CHECKF(c.cx, 3200); CHECKF(c.cy, 1200);
}
