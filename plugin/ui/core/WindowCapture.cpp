// Non-Apple counterpart of WindowCapture.mm. On Linux the window's pixels are
// read back through Xlib, which includes the hosted editor's own child window;
// elsewhere there is no capture and the effect slot keeps its default tile.
#include "WindowCapture.h"

#if JUCE_LINUX || JUCE_BSD

// After the JUCE headers: Xlib's macros (None, Bool, Status...) clash with them.
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace t3k::ui {

namespace {

int ignoreXError(Display*, XErrorEvent*) { return 0; }

int maskShift(unsigned long mask) {
  int shift = 0;
  while (mask != 0 && (mask & 1) == 0) {
    mask >>= 1;
    ++shift;
  }
  return shift;
}

}  // namespace

juce::Image captureComponentWindow(juce::Component& component) {
  auto* peer = component.getPeer();
  if (peer == nullptr)
    return {};
  const auto window = static_cast<::Window>(reinterpret_cast<juce::pointer_sized_uint>(peer->getNativeHandle()));
  if (window == 0)
    return {};

  // A private connection: JUCE's own Display is not exposed, and window ids are
  // server-wide. Errors (e.g. the window just closed) must not hit Xlib's
  // default handler, which exits the process.
  Display* display = XOpenDisplay(nullptr);
  if (display == nullptr)
    return {};
  auto* previousHandler = XSetErrorHandler(ignoreXError);

  juce::Image result;
  XWindowAttributes attributes{};
  if (XGetWindowAttributes(display, window, &attributes) != 0 &&
      attributes.map_state == IsViewable && attributes.width > 0 && attributes.height > 0) {
    if (XImage* image = XGetImage(display, window, 0, 0, static_cast<unsigned>(attributes.width),
                                  static_cast<unsigned>(attributes.height), AllPlanes, ZPixmap)) {
      const int rs = maskShift(image->red_mask), gs = maskShift(image->green_mask),
                bs = maskShift(image->blue_mask);
      const auto rm = image->red_mask >> rs, gm = image->green_mask >> gs, bm = image->blue_mask >> bs;
      if (rm != 0 && gm != 0 && bm != 0) {
        result = juce::Image(juce::Image::RGB, image->width, image->height, false);
        juce::Image::BitmapData out(result, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < image->height; ++y)
          for (int x = 0; x < image->width; ++x) {
            const auto pixel = XGetPixel(image, x, y);
            out.setPixelColour(x, y, juce::Colour(
                static_cast<juce::uint8>(((pixel >> rs) & rm) * 255 / rm),
                static_cast<juce::uint8>(((pixel >> gs) & gm) * 255 / gm),
                static_cast<juce::uint8>(((pixel >> bs) & bm) * 255 / bm)));
          }
      }
      XDestroyImage(image);
    }
  }

  XSync(display, False);
  XSetErrorHandler(previousHandler);
  XCloseDisplay(display);
  return result;
}

}  // namespace t3k::ui

#elif !JUCE_MAC

namespace t3k::ui {

juce::Image captureComponentWindow(juce::Component&) { return {}; }

}  // namespace t3k::ui

#endif
