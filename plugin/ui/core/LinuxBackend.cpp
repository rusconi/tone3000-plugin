#include "LinuxBackend.h"

#include <cstdio>
#include <cstdlib>

#if JUCE_LINUX || JUCE_BSD
#include <unistd.h>
#endif

namespace t3k::ui::linuxbackend {

juce::String discoverWaylandDisplay(const juce::String& currentValue,
                                    const juce::String& runtimeDir,
                                    const std::function<bool(const juce::String&)>& socketExists) {
  if (currentValue.isNotEmpty()) return currentValue;
  if (runtimeDir.isEmpty()) return {};
  for (int i = 0; i < kMaxDiscoveredDisplays; ++i) {
    const auto name = "wayland-" + juce::String(i);
    if (socketExists(runtimeDir + "/" + name)) return name;
  }
  return {};
}

#if JUCE_LINUX || JUCE_BSD
namespace {

bool socketExists(const juce::String& path) { return ::access(path.toRawUTF8(), F_OK) == 0; }

juce::String waylandSocketPath(const juce::String& display, const juce::String& runtimeDir) {
  if (display.isEmpty()) return {};
  // Wayland treats a name containing '/' as a socket path; bare names live in
  // XDG_RUNTIME_DIR (wl_display_connect does the same lookup).
  if (display.startsWithChar('/')) return display;
  if (runtimeDir.isEmpty()) return {};
  return runtimeDir + "/" + display;
}

#if defined(T3K_UI_COARSE_POINTER)
// Runs before main(), i.e. before JUCE opens a display: NativeEditor's first
// member queries the displays and the standalone window constructs the editor
// before adding it to the desktop, so a constructor-body fix would already be
// too late. Kiosk builds only — core/ is compiled into the plugin formats too,
// where setting the host process's WAYLAND_DISPLAY would be a silent
// side effect (plugin windows stay on X11 by design).
struct WaylandStartupGuard {
  WaylandStartupGuard() { ensureWaylandDisplay(); }
};
[[maybe_unused]] const WaylandStartupGuard waylandStartupGuard;
#endif

}  // namespace

juce::String ensureWaylandDisplay() {
  const auto runtimeDir = juce::SystemStats::getEnvironmentVariable("XDG_RUNTIME_DIR", {});
  const auto current = juce::SystemStats::getEnvironmentVariable("WAYLAND_DISPLAY", {});
  const auto display = discoverWaylandDisplay(current, runtimeDir, socketExists);
  if (current.isNotEmpty() || display.isEmpty()) return display;

  ::setenv("WAYLAND_DISPLAY", display.toRawUTF8(), 1);
  // Plain stderr: this can run before JUCE's logger and message loop exist.
  std::fprintf(stderr, "[Startup] WAYLAND_DISPLAY=%s (auto-detected in %s)\n",
               display.toRawUTF8(), runtimeDir.toRawUTF8());
  return display;
}

bool usingWaylandDisplay() {
  const auto runtimeDir = juce::SystemStats::getEnvironmentVariable("XDG_RUNTIME_DIR", {});
  const auto display = juce::SystemStats::getEnvironmentVariable("WAYLAND_DISPLAY", {});
  const auto path = waylandSocketPath(display, runtimeDir);
  return path.isNotEmpty() && socketExists(path);
}
#endif

}  // namespace t3k::ui::linuxbackend

