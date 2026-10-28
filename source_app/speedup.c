#include "speedup.h"

#include <3ds.h>

static LightLock speedup_lock;
static int speedup_count;

void speedup_init(void) {
  LightLock_Init(&speedup_lock);
  speedup_count = 0;
}

void speedup_begin(void) {
  LightLock_Lock(&speedup_lock);
  if(speedup_count++ == 0) osSetSpeedupEnable(true);
  LightLock_Unlock(&speedup_lock);
}

void speedup_end(void) {
  LightLock_Lock(&speedup_lock);
  if(speedup_count > 0 && --speedup_count == 0) osSetSpeedupEnable(false);
  LightLock_Unlock(&speedup_lock);
}

void speedup_exit(void) {
  LightLock_Lock(&speedup_lock);
  speedup_count = 0;
  osSetSpeedupEnable(false);
  LightLock_Unlock(&speedup_lock);
}
