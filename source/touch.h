// touch.h — virtual gamepad over the bottom game (v1.1 touch Stage 1).
// Single-touch: one zone (GBA key) held at a time. Touch drives the bottom-screen game.
#pragma once
#include <3ds.h>

// GBA key mask for the zone under (px,py) on the 320x240 bottom screen, or 0 for none.
u16  touch_keys(int px, int py);

// Draw the translucent gamepad overlay on the already-bound bottom target; `held` lights the pressed zone.
void touch_overlay(u16 held);
