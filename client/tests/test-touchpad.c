#include <assert.h>
#include <string.h>

#include "touchpad.h"

#ifndef SPICE_MOUSE_BUTTON_MASK_LEFT
#define SPICE_MOUSE_BUTTON_MASK_LEFT   (1 << 0)
#endif

typedef struct
{
  int frameCount;
  int warnCount;
  PSTouchpadFrame frames[16];
  PSTouchpadContact contacts[16][PS_TOUCHPAD_MAX_CONTACTS];
}
Capture;

static bool captureFrame(const PSTouchpadFrame * frame, void * opaque)
{
  Capture * capture = (Capture *)opaque;
  assert(capture->frameCount < 16);

  PSTouchpadFrame * dst = &capture->frames[capture->frameCount];
  *dst = *frame;
  memcpy(capture->contacts[capture->frameCount], frame->contacts,
      frame->contact_count * sizeof(*frame->contacts));
  dst->contacts = capture->contacts[capture->frameCount];
  ++capture->frameCount;
  return true;
}

static void captureWarn(const char * msg, void * opaque)
{
  (void)msg;
  Capture * capture = (Capture *)opaque;
  ++capture->warnCount;
}

static struct input_event ev(uint16_t type, uint16_t code, int32_t value)
{
  struct input_event event;
  memset(&event, 0, sizeof(event));
  event.type = type;
  event.code = code;
  event.value = value;
  return event;
}

static void handle(LGTouchpad * touchpad, Capture * capture,
    uint16_t type, uint16_t code, int32_t value)
{
  struct input_event event = ev(type, code, value);
  lg_touchpadHandleEvent(touchpad, &event, true, 1234,
      captureFrame, capture, captureWarn, capture);
}

static LGTouchpad newTouchpad(void)
{
  LGTouchpad touchpad;
  lg_touchpadInit(&touchpad);
  touchpad.absX.minimum = 0;
  touchpad.absX.maximum = 1000;
  touchpad.absY.minimum = 0;
  touchpad.absY.maximum = 1000;
  touchpad.absPressure.minimum = 0;
  touchpad.absPressure.maximum = 100;
  touchpad.hasPressure = true;
  return touchpad;
}

static void testSingleFinger(void)
{
  LGTouchpad touchpad = newTouchpad();
  Capture capture = {0};

  handle(&touchpad, &capture, EV_ABS, ABS_MT_SLOT, 0);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_TRACKING_ID, 10);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_POSITION_X, 500);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_POSITION_Y, 250);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_PRESSURE, 50);
  handle(&touchpad, &capture, EV_SYN, SYN_REPORT, 0);

  assert(capture.frameCount == 1);
  assert(capture.frames[0].contact_count == 1);
  assert(capture.frames[0].active_count == 1);
  assert(capture.frames[0].contacts[0].contact_id == 0);
  assert(capture.frames[0].contacts[0].state == PS_TOUCHPAD_CONTACT_DOWN);
  assert(capture.frames[0].contacts[0].x == 32767);
  assert(capture.frames[0].contacts[0].y == 16383);
  assert(capture.frames[0].contacts[0].pressure == 32767);

  handle(&touchpad, &capture, EV_ABS, ABS_MT_POSITION_X, 1000);
  handle(&touchpad, &capture, EV_SYN, SYN_REPORT, 0);
  assert(capture.frameCount == 2);
  assert(capture.frames[1].contacts[0].state == PS_TOUCHPAD_CONTACT_MOTION);
  assert(capture.frames[1].contacts[0].x == 65535);

  handle(&touchpad, &capture, EV_ABS, ABS_MT_TRACKING_ID, -1);
  handle(&touchpad, &capture, EV_SYN, SYN_REPORT, 0);
  assert(capture.frameCount == 3);
  assert(capture.frames[2].contacts[0].state == PS_TOUCHPAD_CONTACT_UP);
  assert(capture.frames[2].active_count == 0);
}

static void testTwoFingerAndButton(void)
{
  LGTouchpad touchpad = newTouchpad();
  Capture capture = {0};

  handle(&touchpad, &capture, EV_KEY, BTN_LEFT, 1);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_SLOT, 0);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_TRACKING_ID, 10);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_POSITION_X, 100);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_POSITION_Y, 200);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_SLOT, 1);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_TRACKING_ID, 11);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_POSITION_X, 800);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_POSITION_Y, 900);
  handle(&touchpad, &capture, EV_SYN, SYN_REPORT, 0);

  assert(capture.frameCount == 1);
  assert(capture.frames[0].buttons_state == SPICE_MOUSE_BUTTON_MASK_LEFT);
  assert(capture.frames[0].contact_count == 2);
  assert(capture.frames[0].active_count == 2);
  assert(capture.frames[0].contacts[0].contact_id == 0);
  assert(capture.frames[0].contacts[1].contact_id == 1);

  handle(&touchpad, &capture, EV_KEY, BTN_LEFT, 0);
  handle(&touchpad, &capture, EV_SYN, SYN_REPORT, 0);

  assert(capture.frameCount == 2);
  assert(capture.frames[1].buttons_state == 0);
  assert(capture.frames[1].contact_count == 0);
  assert(capture.frames[1].active_count == 2);
}

static void testSynDroppedAndOverflow(void)
{
  LGTouchpad touchpad = newTouchpad();
  Capture capture = {0};

  handle(&touchpad, &capture, EV_ABS, ABS_MT_SLOT, 0);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_TRACKING_ID, 10);
  handle(&touchpad, &capture, EV_SYN, SYN_REPORT, 0);
  handle(&touchpad, &capture, EV_SYN, SYN_DROPPED, 0);

  assert(capture.frameCount == 2);
  assert(capture.frames[1].contacts[0].state == PS_TOUCHPAD_CONTACT_CANCEL);
  assert(capture.frames[1].active_count == 0);

  handle(&touchpad, &capture, EV_ABS, ABS_MT_SLOT, PS_TOUCHPAD_MAX_CONTACTS);
  handle(&touchpad, &capture, EV_ABS, ABS_MT_SLOT, PS_TOUCHPAD_MAX_CONTACTS + 1);
  assert(capture.warnCount == 1);
}

int main(void)
{
  testSingleFinger();
  testTwoFingerAndButton();
  testSynDroppedAndOverflow();
  return 0;
}
