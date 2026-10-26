#include "test.h"
#include "littlemenu.h"

static tui_menu_result cancel_callback(tui_menu_item_data * data, tui_menu_unit_t pos, tui_menu_action action) {
  return TUIMENU_CANCELRESULT;
}

void test_littlemenu(void) {
  const tui_menu_action accept = { .action = TUIMENU_ACTION_ACCEPT };
  const tui_menu_action down = { .action = TUIMENU_ACTION_MOVE, .offset = 1 };
  const tui_menu_action up = { .action = TUIMENU_ACTION_MOVE, .offset = -1 };
  int err = 0;

  // #1, #2: empty menu
  tui_menu empty;
  tui_menu_init(&empty, 5);
  tui_menu_result r = tui_menu_run(&empty, accept);   // used to deref NULL
  CHECK(r.result < 0 && r.error == 0);
  tui_menu_run(&empty, down);
  CHECK(empty.current == 0);                          // used to become -1
  tui_menu_run(&empty, up);
  CHECK(empty.current == 0);
  tui_menu_free(&empty);

  // #3: parent set manually, no submenu item -> accept and cancel must not crash
  tui_menu root, child;
  tui_menu_init(&root, 5);
  tui_menu_init(&child, 5);
  TUIMITEM_BASIC(&root, err, "r0", 0);
  TUIMITEM_BASIC(&child, err, "c0", 0);
  TUIMITEM_BASIC(&child, err, "c1", 0);
  CHECK(!err);
  child.parent = &root;
  tui_menu_run(&child, down);
  r = tui_menu_run(&child, accept);
  CHECK(r.result == 1 && !r.cancelled);
  // #5: depth counts parents
  CHECK(r.depth == 1);
  // A three level manual chain for get_exit_parent's grandparent lookup
  tui_menu grand;
  tui_menu_init(&grand, 5);
  TUIMITEM_BASIC(&grand, err, "g0", 0);
  grand.parent = &child;
  r = tui_menu_run(&grand, TUIMENU_CANCELACTION);
  CHECK(r.cancelled && r.depth == 2);
  tui_menu_free(&grand);
  tui_menu_free(&child);

  // #5: depth through real submenus, root = 0
  tui_menu sub;
  tui_menu_init(&sub, 5);
  TUIMITEM_BASIC(&sub, err, "s0", 0);
  TUIMITEM_SUBMENU_EXISTING(&root, err, "open sub", &sub);
  CHECK(!err);
  r = tui_menu_run(&root, accept);
  CHECK(r.depth == 0 && r.result == 0);   // r0 is basic
  tui_menu_run(&root, down);
  tui_menu_run(&root, accept);            // opens sub
  r = tui_menu_run(&root, accept);        // accepts s0 inside sub
  CHECK(r.result == 0 && r.depth == 1);

  // #4: callback returning TUIMENU_CANCELRESULT reports cancelled, both directly and via run
  tui_menu_result cr = TUIMENU_CANCELRESULT;
  CHECK(cr.cancelled == 1 && cr.result == -1);
  tui_menu_item_data nodata = {0};
  TUIMITEM_CALLBACK(&sub, err, "cancel me", cancel_callback, nodata);
  CHECK(!err);
  tui_menu_run(&root, down);
  r = tui_menu_run(&root, accept);
  CHECK(r.cancelled && r.result < 0);
  tui_menu_free(&root);
  tui_menu_free(&sub);
}
