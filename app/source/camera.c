// The outer camera through cam:u (libctru 2.7.0 cam.h; the order of calls as in the camera/video example of
// devkitPro's 3ds-examples in the image), polled from the main loop instead of a thread, and quirc for the codes.
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#include "camera.h"
#include "console.h"
#include "quirc.h"

#define FRAME_BYTES (CAMERA_W * CAMERA_H_PX * 2)
#define TIMEOUT_RESULT ((Result)0x09401BFE)    // svcWaitSynchronizationN with nothing signalled yet

static struct {
    int running, interrupted;
    u32 unit;
    Handle error_event, receive_event;
    uint16_t *receiving, *ready;
    struct quirc *qr;
} cam;

int camera_running(void)
{
    return cam.running;
}

void camera_stop(void)
{
    if (cam.running) {
        CAMU_StopCapture(PORT_CAM1);
        bool busy = true;
        for (int i = 0; i < 100 && R_SUCCEEDED(CAMU_IsBusy(&busy, PORT_CAM1)) && busy; i++)
            svcSleepThread(1000000);
        CAMU_ClearBuffer(PORT_CAM1);
        CAMU_Activate(SELECT_NONE);
        camExit();
    }
    if (cam.receive_event)
        svcCloseHandle(cam.receive_event);
    if (cam.error_event)
        svcCloseHandle(cam.error_event);
    free(cam.receiving);
    free(cam.ready);
    if (cam.qr)
        quirc_destroy(cam.qr);
    memset(&cam, 0, sizeof cam);
}

static Result step(const char *call, Result r)
{
    if (R_FAILED(r))
        console_log("%-38s %08lX FAILED", call, (unsigned long)r);
    return r;
}

int camera_start(void)
{
    camera_stop();
    cam.receiving = malloc(FRAME_BYTES);
    cam.ready = malloc(FRAME_BYTES);
    cam.qr = quirc_new();
    if (!cam.receiving || !cam.ready || !cam.qr || quirc_resize(cam.qr, CAMERA_W, CAMERA_H_PX) < 0) {
        camera_stop();
        return -1;
    }
    if (R_FAILED(step("camInit", camInit()))) {
        camera_stop();
        return -1;
    }
    cam.running = 1;
    if (R_FAILED(step("CAMU_SetSize", CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A))) ||
        R_FAILED(step("CAMU_SetOutputFormat", CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A))) ||
        R_FAILED(step("CAMU_SetFrameRate", CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30))) ||
        R_FAILED(step("CAMU_SetNoiseFilter", CAMU_SetNoiseFilter(SELECT_OUT1, true))) ||
        R_FAILED(step("CAMU_SetAutoExposure", CAMU_SetAutoExposure(SELECT_OUT1, true))) ||
        R_FAILED(step("CAMU_SetAutoWhiteBalance", CAMU_SetAutoWhiteBalance(SELECT_OUT1, true))) ||
        R_FAILED(step("CAMU_SetTrimming", CAMU_SetTrimming(PORT_CAM1, false))) ||
        R_FAILED(step("CAMU_GetMaxBytes", CAMU_GetMaxBytes(&cam.unit, CAMERA_W, CAMERA_H_PX))) ||
        R_FAILED(step("CAMU_SetTransferBytes", CAMU_SetTransferBytes(PORT_CAM1, cam.unit, CAMERA_W, CAMERA_H_PX))) ||
        R_FAILED(step("CAMU_Activate", CAMU_Activate(SELECT_OUT1))) ||
        R_FAILED(step("CAMU_GetBufferErrorInterruptEvent",
                      CAMU_GetBufferErrorInterruptEvent(&cam.error_event, PORT_CAM1))) ||
        R_FAILED(step("CAMU_ClearBuffer", CAMU_ClearBuffer(PORT_CAM1))) ||
        R_FAILED(step("CAMU_StartCapture", CAMU_StartCapture(PORT_CAM1)))) {
        camera_stop();
        return -1;
    }
    return 0;
}

int camera_frame(const uint16_t **frame)
{
    if (!cam.running)
        return 0;
    if (!cam.receive_event &&
        R_FAILED(CAMU_SetReceiving(&cam.receive_event, cam.receiving, PORT_CAM1, FRAME_BYTES, (s16)cam.unit)))
        return 0;
    if (cam.interrupted) {
        CAMU_StartCapture(PORT_CAM1);
        cam.interrupted = 0;
    }
    Handle events[2] = {cam.error_event, cam.receive_event};
    s32 which = -1;
    Result r = svcWaitSynchronizationN(&which, events, 2, false, 0);
    if (r == TIMEOUT_RESULT || R_FAILED(r))
        return 0;
    svcCloseHandle(cam.receive_event);
    cam.receive_event = 0;
    if (which == 0) {          // a buffer error stopped the capture: start it again on the next poll
        cam.interrupted = 1;
        return 0;
    }
    uint16_t *done = cam.receiving;
    cam.receiving = cam.ready;
    cam.ready = done;
    *frame = done;
    return 1;
}

int camera_qr(const uint16_t *frame, char *text, size_t size)
{
    if (!cam.qr || !size)
        return 0;
    int w, h;
    uint8_t *gray = quirc_begin(cam.qr, &w, &h);
    if (w != CAMERA_W || h != CAMERA_H_PX)
        return 0;
    for (int i = 0; i < CAMERA_W * CAMERA_H_PX; i++) {
        uint16_t p = frame[i];
        unsigned r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
        gray[i] = (uint8_t)((r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8);
    }
    quirc_end(cam.qr);
    for (int k = 0; k < quirc_count(cam.qr); k++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(cam.qr, k, &code);
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS)
            continue;
        size_t n = (size_t)data.payload_len < size - 1 ? (size_t)data.payload_len : size - 1;
        memcpy(text, data.payload, n);
        text[n] = 0;
        return 1;
    }
    return 0;
}
