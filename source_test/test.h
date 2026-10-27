#ifndef __HEADER_JIGSAW_TEST__
#define __HEADER_JIGSAW_TEST__

#include <stdio.h>
#include <math.h>
#include <stdint.h>

extern int test_failures;
extern int test_checks;

#define CHECK(cond) do { test_checks++; if(!(cond)) { test_failures++; \
  printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while(0)
#define CHECKF(a, b) CHECK(fabsf((float)(a) - (float)(b)) < 0.001f)

void test_board(void);
void test_drawlist(void);
void test_musicdec(void);
void test_musicdec_files(const char * dir);
void test_boardsave(void);
void test_savefile(void);
void test_sfxgen(void);
void test_storage(void);
void test_dump_sfx(const char * dir);
void test_camera(void);
void test_stick(void);
void test_layout(void);
void test_edgegen(void);
void test_edgegen_irregular(void);
void test_imgscale(void);
void test_swizzle(void);
void test_piecebake(void);
void test_littlemenu(void);
void test_dump_cut(const char * path, uint8_t style);  // EDGEGEN_STYLE_*

#endif
