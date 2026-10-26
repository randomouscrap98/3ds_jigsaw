#ifndef __HEADER_JIGSAW_TEST__
#define __HEADER_JIGSAW_TEST__

#include <stdio.h>
#include <math.h>

extern int test_failures;
extern int test_checks;

#define CHECK(cond) do { test_checks++; if(!(cond)) { test_failures++; \
  printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while(0)
#define CHECKF(a, b) CHECK(fabsf((float)(a) - (float)(b)) < 0.001f)

void test_board(void);
void test_boardsave(void);
void test_camera(void);
void test_layout(void);
void test_edgegen(void);
void test_imgscale(void);
void test_swizzle(void);
void test_piecebake(void);
void test_dump_cut(const char * path);

#endif
