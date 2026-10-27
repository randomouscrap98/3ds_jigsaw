#include "test.h"
#include "edgegen.h"
#include "cutter.h"

#include <string.h>

int test_failures = 0;
int test_checks = 0;

#define RUN(fn) do { printf("%s\n", #fn); fn(); } while(0)

int main(int argc, char ** argv) {
  if(argc > 2 && !strcmp(argv[1], "sfx")) {
    test_dump_sfx(argv[2]);
    return 0;
  }
  if(argc > 2 && !strcmp(argv[1], "musicdec")) {
    test_musicdec_files(argv[2]);
    printf("%d checks, %d failures\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
  }
  if(argc > 2 && !strcmp(argv[1], "dump")) {
    uint8_t style = EDGEGEN_STYLE_CLASSIC;
    if(argc > 3 && !strcmp(argv[3], "irregular")) style = EDGEGEN_STYLE_IRREGULAR;
    if(argc > 3 && !strcmp(argv[3], "wild")) style = EDGEGEN_STYLE_WILD;
    if(argc > 3 && !strcmp(argv[3], "random")) style = CUT_STYLE_RANDOM;
    test_dump_cut(argv[2], style);
    return 0;
  }
  RUN(test_board);
  RUN(test_piecegeo);
  RUN(test_randcut);
  RUN(test_cutgraph_grid);
  RUN(test_whimsy);
  RUN(test_boardsave_whimsy);
  RUN(test_drawlist);
  RUN(test_musicdec);
  RUN(test_boardsave);
  RUN(test_savefile);
  RUN(test_sfxgen);
  RUN(test_storage);
  RUN(test_webup);
  RUN(test_camera);
  RUN(test_stick);
  RUN(test_layout);
  RUN(test_edgegen);
  RUN(test_cut_pinned);
  RUN(test_edgegen_irregular);
  RUN(test_imgscale);
  RUN(test_swizzle);
  RUN(test_piecebake);
  RUN(test_littlemenu);
  printf("%d checks, %d failures\n", test_checks, test_failures);
  return test_failures ? 1 : 0;
}
