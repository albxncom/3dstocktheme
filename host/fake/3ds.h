/* For host/test_main.c only: a stand-in for libctru with just what app/source/main.c uses. The key names are
 * libctru's; their values only need to differ. The test drives the touch screen, the keys and the clock. */
#ifndef FAKE_3DS_H
#define FAKE_3DS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32;
typedef int32_t Result; typedef u32 Handle;
#define BIT(n) (1U << (n))
enum {
    KEY_A = BIT(0), KEY_B = BIT(1), KEY_SELECT = BIT(2), KEY_START = BIT(3), KEY_DRIGHT = BIT(4), KEY_DLEFT = BIT(5),
    KEY_DUP = BIT(6), KEY_DDOWN = BIT(7), KEY_R = BIT(8), KEY_L = BIT(9), KEY_X = BIT(10), KEY_Y = BIT(11),
    KEY_ZL = BIT(14), KEY_ZR = BIT(15), KEY_TOUCH = BIT(20), KEY_CPAD_RIGHT = BIT(28), KEY_CPAD_LEFT = BIT(29),
    KEY_CPAD_UP = BIT(30), KEY_CPAD_DOWN = BIT(31),
    KEY_UP = KEY_DUP | KEY_CPAD_UP, KEY_DOWN = KEY_DDOWN | KEY_CPAD_DOWN, KEY_LEFT = KEY_DLEFT | KEY_CPAD_LEFT,
    KEY_RIGHT = KEY_DRIGHT | KEY_CPAD_RIGHT,
};
typedef struct { u16 px, py; } touchPosition;
typedef enum { APTHOOK_ONSUSPEND, APTHOOK_ONRESTORE, APTHOOK_ONSLEEP, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT } APT_HookType;
typedef void (*aptHookFn)(APT_HookType hook, void *param);
typedef struct aptHookCookie { int dummy; } aptHookCookie;
typedef struct Thread_tag *Thread;
typedef void (*ThreadFunc)(void *);
#define CUR_THREAD_HANDLE 0xFFFF8000
#define U64_MAX UINT64_MAX
void hidScanInput(void); u32 hidKeysDown(void); u32 hidKeysHeld(void); u32 hidKeysUp(void); u32 hidKeysDownRepeat(void);
void hidTouchRead(touchPosition *t); void hidSetRepeatParameters(u32 a, u32 b);
u64 osGetTime(void);
#define CPU_TICKS_PER_MSEC 268111.856
u64 svcGetSystemTick(void);
bool aptMainLoop(void); void aptSetHomeAllowed(bool a); void aptSetSleepAllowed(bool a);
void aptHook(aptHookCookie *c, aptHookFn fn, void *p); void aptUnhook(aptHookCookie *c);
Thread threadCreate(ThreadFunc fn, void *arg, size_t stack, int prio, int core, bool detached);
Result threadJoin(Thread t, u64 ns); void threadFree(Thread t);
Result svcGetThreadPriority(s32 *prio, Handle h); void svcSleepThread(int64_t ns);
#endif
