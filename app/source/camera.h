/* The outer camera and QR decoding (quirc 1.2, vendored in app/vendor/quirc). Frames are 400x240 RGB565, as the
 * camera service delivers them for the top screen's size. Console only. */
#ifndef CAMERA_H
#define CAMERA_H

#include <stddef.h>
#include <stdint.h>

#define CAMERA_W 400
#define CAMERA_H_PX 240

/* Starts the outer camera (CAM1) and the decoder; 0 on success. camera_stop undoes everything, also after a
 * failed start, and is safe to call twice. */
int camera_start(void);
void camera_stop(void);
int camera_running(void);

/* Polls without waiting: 1 and *frame when a new frame is complete, else 0. The frame stays valid until the next
 * call. */
int camera_frame(const uint16_t **frame);

/* Looks for a QR code in a frame; 1 and its text (NUL-terminated, cut to size) when one decodes. */
int camera_qr(const uint16_t *frame, char *text, size_t size);

#endif
