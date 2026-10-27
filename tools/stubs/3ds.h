// Minimal stand-ins for the libctru API used by jigsaw, ONLY for `gcc -fsyntax-only`
// checks on machines without devkitPro (tools/stubcheck.sh). Not the real API.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef s32 Result;
#define R_FAILED(r) ((r) < 0)
enum { KEY_A = 1, KEY_B = 2, KEY_SELECT = 4, KEY_START = 8, KEY_DRIGHT = 16, KEY_DLEFT = 32,
  KEY_DUP = 64, KEY_DDOWN = 128, KEY_R = 256, KEY_L = 512, KEY_X = 1024, KEY_Y = 2048,
  KEY_TOUCH = 1 << 20 };
typedef struct { u16 px, py; } touchPosition;
typedef struct { s16 dx, dy; } circlePosition;
void hidScanInput(void); u32 hidKeysDown(void); u32 hidKeysHeld(void); u32 hidKeysUp(void);
u32 hidKeysDownRepeat(void); void hidTouchRead(touchPosition *); void hidCircleRead(circlePosition *);
void hidSetRepeatParameters(u32 delay, u32 interval);
typedef enum { GFX_TOP, GFX_BOTTOM } gfxScreen_t;
typedef enum { GFX_LEFT, GFX_RIGHT } gfx3dSide_t;
void gfxInitDefault(void); void gfxExit(void);
bool aptMainLoop(void);
typedef enum { APTHOOK_ONSUSPEND, APTHOOK_ONRESTORE, APTHOOK_ONSLEEP, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT } APT_HookType;
typedef void (*aptHookFn)(APT_HookType hook, void * param);
typedef struct { void * next; aptHookFn callback; void * param; } aptHookCookie;
void aptHook(aptHookCookie * cookie, aptHookFn callback, void * param);
void aptUnhook(aptHookCookie * cookie);
u64 osGetTime(void);
u64 svcGetSystemTick(void);
#define SYSCLOCK_ARM11 268111856
Result archive_getmtime(const char * name, u64 * mtime);
void * linearAlloc(size_t size); void linearFree(void * mem);
Result DSP_FlushDataCache(const void * address, u32 size);
enum { NDSP_OUTPUT_MONO, NDSP_OUTPUT_STEREO };
enum { NDSP_INTERP_POLYPHASE, NDSP_INTERP_LINEAR, NDSP_INTERP_NONE };
enum { NDSP_FORMAT_MONO_PCM16 = 5, NDSP_FORMAT_STEREO_PCM16 = 6 };
enum { NDSP_WBUF_FREE, NDSP_WBUF_QUEUED, NDSP_WBUF_PLAYING, NDSP_WBUF_DONE };
typedef struct { union { const void * data_vaddr; s16 * data_pcm16; }; u32 nsamples; u8 status; bool looping; } ndspWaveBuf;
typedef void (*ndspCallback)(void * data); void ndspSetCallback(ndspCallback callback, void * data);
Result ndspInit(void); void ndspExit(void); void ndspSetOutputMode(int mode);
void ndspChnReset(int id); void ndspChnSetInterp(int id, int type); void ndspChnSetRate(int id, float rate);
void ndspChnSetFormat(int id, u16 format); void ndspChnSetMix(int id, float mix[12]);
void ndspChnWaveBufClear(int id); void ndspChnWaveBufAdd(int id, ndspWaveBuf * buf);
#define R_SUCCEEDED(r) ((r) >= 0)
Result romfsInit(void); Result romfsExit(void);
Result socInit(u32 * context_addr, u32 context_size); Result socExit(void);
typedef struct Thread_tag * Thread;
typedef void (*ThreadFunc)(void *);
Thread threadCreate(ThreadFunc entrypoint, void * arg, size_t stack_size, int prio, int core_id, bool detached);
Result threadJoin(Thread thread, u64 timeout_ns); void threadFree(Thread thread);
typedef s32 LightLock; void LightLock_Init(LightLock * lock); void LightLock_Lock(LightLock * lock); void LightLock_Unlock(LightLock * lock);
typedef enum { RESET_ONESHOT, RESET_STICKY, RESET_PULSE } ResetType;
typedef struct { s32 state; LightLock lock; } LightEvent;
void LightEvent_Init(LightEvent * event, ResetType reset_type); void LightEvent_Signal(LightEvent * event); void LightEvent_Wait(LightEvent * event);
typedef u32 Handle;
#define CUR_THREAD_HANDLE 0xFFFF8000
Result svcGetThreadPriority(s32 * out, Handle handle); void svcSleepThread(s64 ns);
#ifndef U64_MAX
#define U64_MAX UINT64_MAX
#endif
void hidCstickRead(circlePosition * pos);
