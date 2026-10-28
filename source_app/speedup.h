#ifndef __HEADER_JIGSAW_SPEEDUP__
#define __HEADER_JIGSAW_SPEEDUP__

// New 3DS speedup (804 MHz + L2 cache) only while there's heavy work: loading or starting a
// puzzle, making a menu thumbnail. Off the rest of the time (play, menus) to save battery.
// Counted, so overlapping work on several threads is fine: the first begin turns it on, the
// last end turns it off. No effect on an Old 3DS. libctru re-applies the current setting
// after HOME/sleep.

void speedup_init(void);
void speedup_begin(void);
void speedup_end(void);
// Off, whatever the count (app exit)
void speedup_exit(void);

#endif
