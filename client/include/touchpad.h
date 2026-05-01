/**
 * Looking Glass
 * Copyright © 2017-2025 The Looking Glass Authors
 * https://looking-glass.io
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <linux/input.h>
#include <purespice.h>

typedef bool (*LGTouchpadSendFrame)(const PSTouchpadFrame * frame, void * opaque);
typedef void (*LGTouchpadWarn)(const char * msg, void * opaque);

typedef struct
{
  bool active;
  bool wasActive;
  bool dirty;
  int  trackingId;
  int  x;
  int  y;
  int  pressure;
  int  touchMajor;
  int  touchMinor;
}
LGTouchpadSlot;

typedef struct
{
  struct input_absinfo absX;
  struct input_absinfo absY;
  struct input_absinfo absPressure;
  struct input_absinfo absTouchMajor;
  struct input_absinfo absTouchMinor;
  bool hasPressure;
  bool hasTouchMajor;
  bool hasTouchMinor;

  uint16_t buttons;
  bool buttonsDirty;
  bool slotOverflowWarned;
  int currentSlot;
  LGTouchpadSlot slots[PS_TOUCHPAD_MAX_CONTACTS];
}
LGTouchpad;

void lg_touchpadInit(LGTouchpad * touchpad);
void lg_touchpadHandleEvent(
    LGTouchpad * touchpad,
    const struct input_event * ev,
    bool forward,
    uint64_t timeUsec,
    LGTouchpadSendFrame sendFrame,
    void * sendOpaque,
    LGTouchpadWarn warn,
    void * warnOpaque);
