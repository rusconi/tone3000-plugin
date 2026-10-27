// On-screen keyboard (OSK) occlusion height.
//
// On Raspberry Pi OS (labwc + squeekboard) the compositor's keyboard is an
// overlay that covers the bottom of the window rather than resizing it, so a
// focused text field near the bottom can end up hidden underneath the keys.
// DragScroller reads this height to keep a focused TextEditor scrolled above
// the keyboard.
//
// T3K_OSK_HEIGHT is the keyboard's on-screen height in design px at editor
// scale 1 (squeekboard's 4-row layout measures ~205 at 1024 wide, so the
// kiosk sets a little more for a margin). 0 — the default — leaves desktop
// behaviour unchanged, so the same binary is safe on a normal desktop.
#pragma once

#include <juce_core/juce_core.h>

namespace t3k::ui::keyboard {

inline int oskHeight() {
  static const int height = juce::SystemStats::getEnvironmentVariable("T3K_OSK_HEIGHT", "0").getIntValue();
  return height;
}

}  // namespace t3k::ui::keyboard

