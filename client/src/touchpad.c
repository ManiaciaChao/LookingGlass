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

#include "touchpad.h"

#include <string.h>

#ifndef SPICE_MOUSE_BUTTON_MASK_LEFT
#define SPICE_MOUSE_BUTTON_MASK_LEFT   (1 << 0)
#define SPICE_MOUSE_BUTTON_MASK_MIDDLE (1 << 1)
#define SPICE_MOUSE_BUTTON_MASK_RIGHT  (1 << 2)
#endif

static uint16_t normalizeAxis(int value, const struct input_absinfo * abs)
{
  if (abs->maximum <= abs->minimum)
    return 0;

  if (value < abs->minimum)
    value = abs->minimum;
  else if (value > abs->maximum)
    value = abs->maximum;

  uint64_t range = (uint64_t)(abs->maximum - abs->minimum);
  return (uint16_t)(((uint64_t)(value - abs->minimum) * 65535ULL) / range);
}

void lg_touchpadInit(LGTouchpad * touchpad)
{
  memset(touchpad, 0, sizeof(*touchpad));
  touchpad->currentSlot = 0;
}

static void touchpadCancelActive(
    LGTouchpad * touchpad,
    bool forward,
    uint64_t timeUsec,
    LGTouchpadSendFrame sendFrame,
    void * sendOpaque)
{
  PSTouchpadContact contacts[PS_TOUCHPAD_MAX_CONTACTS];
  uint8_t count = 0;

  for(int i = 0; i < PS_TOUCHPAD_MAX_CONTACTS; ++i)
  {
    LGTouchpadSlot * slot = &touchpad->slots[i];
    if (!slot->active && !slot->wasActive)
      continue;

    contacts[count++] = (PSTouchpadContact)
    {
      .contact_id = i,
      .state      = PS_TOUCHPAD_CONTACT_CANCEL,
      .flags      = 0,
      .x          = 0,
      .y          = 0,
    };
  }

  if (forward && count > 0 && sendFrame)
  {
    PSTouchpadFrame frame =
    {
      .time_usec     = timeUsec,
      .buttons_state = touchpad->buttons,
      .contact_count = count,
      .active_count  = 0,
      .contacts      = contacts,
    };
    sendFrame(&frame, sendOpaque);
  }

  lg_touchpadInit(touchpad);
}

static void touchpadHandleReport(
    LGTouchpad * touchpad,
    bool forward,
    uint64_t timeUsec,
    LGTouchpadSendFrame sendFrame,
    void * sendOpaque)
{
  if (!forward || !sendFrame)
  {
    for(int i = 0; i < PS_TOUCHPAD_MAX_CONTACTS; ++i)
      touchpad->slots[i].dirty = false;
    touchpad->buttonsDirty = false;
    return;
  }

  PSTouchpadContact contacts[PS_TOUCHPAD_MAX_CONTACTS];
  uint8_t count = 0;
  uint8_t active = 0;

  for(int i = 0; i < PS_TOUCHPAD_MAX_CONTACTS; ++i)
  {
    LGTouchpadSlot * slot = &touchpad->slots[i];
    if (slot->active)
      ++active;

    if (!slot->dirty && !(slot->wasActive && !slot->active))
      continue;

    uint8_t state = PS_TOUCHPAD_CONTACT_MOTION;
    if (slot->active && !slot->wasActive)
      state = PS_TOUCHPAD_CONTACT_DOWN;
    else if (!slot->active && slot->wasActive)
      state = PS_TOUCHPAD_CONTACT_UP;

    contacts[count++] = (PSTouchpadContact)
    {
      .contact_id   = i,
      .state        = state,
      .flags        = slot->active ? PS_TOUCHPAD_CONTACT_FLAG_TIP |
        PS_TOUCHPAD_CONTACT_FLAG_CONFIDENCE : 0,
      .x            = normalizeAxis(slot->x, &touchpad->absX),
      .y            = normalizeAxis(slot->y, &touchpad->absY),
      .pressure     = touchpad->hasPressure ?
        normalizeAxis(slot->pressure, &touchpad->absPressure) : 0,
      .touch_major  = touchpad->hasTouchMajor ?
        normalizeAxis(slot->touchMajor, &touchpad->absTouchMajor) : 0,
      .touch_minor  = touchpad->hasTouchMinor ?
        normalizeAxis(slot->touchMinor, &touchpad->absTouchMinor) : 0,
    };
  }

  if (count == 0 && !touchpad->buttonsDirty)
    return;

  PSTouchpadFrame frame =
  {
    .time_usec     = timeUsec,
    .buttons_state = touchpad->buttons,
    .contact_count = count,
    .active_count  = active,
    .contacts      = contacts,
  };

  if (sendFrame(&frame, sendOpaque))
  {
    for(int i = 0; i < PS_TOUCHPAD_MAX_CONTACTS; ++i)
    {
      LGTouchpadSlot * slot = &touchpad->slots[i];
      slot->wasActive = slot->active;
      slot->dirty     = false;
    }
    touchpad->buttonsDirty = false;
  }
}

void lg_touchpadHandleEvent(
    LGTouchpad * touchpad,
    const struct input_event * ev,
    bool forward,
    uint64_t timeUsec,
    LGTouchpadSendFrame sendFrame,
    void * sendOpaque,
    LGTouchpadWarn warn,
    void * warnOpaque)
{
  switch(ev->type)
  {
    case EV_ABS:
      if (ev->code == ABS_MT_SLOT)
      {
        if (ev->value < 0 || ev->value >= PS_TOUCHPAD_MAX_CONTACTS)
        {
          if (!touchpad->slotOverflowWarned)
          {
            if (warn)
              warn("Touchpad has more than 16 slots, ignoring high slots",
                  warnOpaque);
            touchpad->slotOverflowWarned = true;
          }
          touchpad->currentSlot = -1;
        }
        else
          touchpad->currentSlot = ev->value;
        break;
      }

      if (touchpad->currentSlot < 0)
        break;

      LGTouchpadSlot * slot = &touchpad->slots[touchpad->currentSlot];
      switch(ev->code)
      {
        case ABS_MT_TRACKING_ID:
          slot->trackingId = ev->value;
          slot->active     = ev->value >= 0;
          slot->dirty      = true;
          break;

        case ABS_MT_POSITION_X:
          slot->x = ev->value;
          slot->dirty = true;
          break;

        case ABS_MT_POSITION_Y:
          slot->y = ev->value;
          slot->dirty = true;
          break;

        case ABS_MT_PRESSURE:
          slot->pressure = ev->value;
          slot->dirty = true;
          break;

        case ABS_MT_TOUCH_MAJOR:
          slot->touchMajor = ev->value;
          slot->dirty = true;
          break;

        case ABS_MT_TOUCH_MINOR:
          slot->touchMinor = ev->value;
          slot->dirty = true;
          break;
      }
      break;

    case EV_KEY:
      switch(ev->code)
      {
        case BTN_LEFT:
        {
          uint16_t oldButtons = touchpad->buttons;
          if (ev->value)
            touchpad->buttons |= SPICE_MOUSE_BUTTON_MASK_LEFT;
          else
            touchpad->buttons &= ~SPICE_MOUSE_BUTTON_MASK_LEFT;
          touchpad->buttonsDirty |= touchpad->buttons != oldButtons;
          break;
        }
        case BTN_RIGHT:
        {
          uint16_t oldButtons = touchpad->buttons;
          if (ev->value)
            touchpad->buttons |= SPICE_MOUSE_BUTTON_MASK_RIGHT;
          else
            touchpad->buttons &= ~SPICE_MOUSE_BUTTON_MASK_RIGHT;
          touchpad->buttonsDirty |= touchpad->buttons != oldButtons;
          break;
        }
        case BTN_MIDDLE:
        {
          uint16_t oldButtons = touchpad->buttons;
          if (ev->value)
            touchpad->buttons |= SPICE_MOUSE_BUTTON_MASK_MIDDLE;
          else
            touchpad->buttons &= ~SPICE_MOUSE_BUTTON_MASK_MIDDLE;
          touchpad->buttonsDirty |= touchpad->buttons != oldButtons;
          break;
        }
      }
      break;

    case EV_SYN:
      if (ev->code == SYN_REPORT)
        touchpadHandleReport(touchpad, forward, timeUsec, sendFrame, sendOpaque);
      else if (ev->code == SYN_DROPPED)
        touchpadCancelActive(touchpad, forward, timeUsec, sendFrame, sendOpaque);
      break;
  }
}
