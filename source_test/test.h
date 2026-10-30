#ifndef __HEADER_JIGSAW_TEST__
#define __HEADER_JIGSAW_TEST__

#include <stdio.h>
#include <math.h>
#include <stdint.h>

#include "board.h"

extern int test_failures;
extern int test_checks;

#define CHECK(cond) do { test_checks++; if(!(cond)) { test_failures++; \
  printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while(0)
#define CHECKF(a, b) CHECK(fabsf((float)(a) - (float)(b)) < 0.001f)

// Board on a straight grid PieceGeo (allocated here, freed by test_board_free)
int test_board_grid(Board * b, uint16_t cols, uint16_t rows, float cell, float width, float height);
void test_board_free(Board * b);

void test_board(void);
void test_piecegeo(void);
void test_randcut(void);
void test_cutgraph_grid(void);
void test_whimsy(void);
void test_boardsave_whimsy(void);
void test_drawlist(void);
void test_musicdec(void);
void test_musicdec_files(const char * dir);
void test_boardsave(void);
void test_savefile(void);
void test_sfxgen(void);
void test_storage(void);
void test_webup(void);
void test_chunkwriter(void);
void test_picfiles(void);
void test_overlay(void);
void test_dump_sfx(const char * dir);
void test_camera(void);
void test_stick(void);
void test_layout(void);
void test_edgegen(void);
void test_cut_pinned(void);
void test_edgegen_irregular(void);
void test_imgscale(void);
void test_swizzle(void);
void test_piecebake(void);
void test_littlemenu(void);
void test_cutsample(void);
void test_randcut_style(uint8_t st);   // RANDCUT_STYLE_*
void test_whimsy_style(uint8_t st);    // CUT_STYLE_* or woodcut style
void test_wood(void);                  // Shelved wooden cuts (tests wood)
void test_dump_cut(const char * path, uint8_t style);  // CUT_STYLE_* or woodcut style

#endif
