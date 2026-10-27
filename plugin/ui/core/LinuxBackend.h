#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace t3k::ui::linuxbackend {

inline constexpr int kMaxDiscoveredDisplays = 16;

juce::String discoverWaylandDisplay(const juce::String& currentValue,
                                    const juce::String& runtimeDir,
                                    const std::function<bool(const juce::String&)>& socketExists);

#if JUCE_LINUX || JUCE_BSD
juce::String ensureWaylandDisplay();
bool usingWaylandDisplay();
#endif

}  // namespace t3k::ui::linuxbackend

