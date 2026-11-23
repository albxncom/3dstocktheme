/* For host/test_main.c only: what main.c and ui.h take from citro2d. */
#ifndef FAKE_C2D_H
#define FAKE_C2D_H
#include <stdint.h>
#include <3ds.h>
static inline uint32_t C2D_Color32(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return r | g << 8 | b << 16 | (uint32_t)a << 24; }
#endif
