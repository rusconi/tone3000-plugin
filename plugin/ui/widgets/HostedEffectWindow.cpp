#include "HostedEffectWindow.h"

#include <map>

#include "backend/Backend.h"
#include "core/WindowCapture.h"

namespace t3k::ui {

namespace {

juce::ChangeBroadcaster& graphicBroadcaster() {
  static juce::ChangeBroadcaster broadcaster;
  return broadcaster;
}

juce::CriticalSection& graphicLock() {
  static juce::CriticalSection lock;
  return lock;
}

std::map<std::string, juce::Image>& graphicMap() {
  static std::map<std::string, juce::Image> images;
  return images;
}

// Graphics saved per plugin (not per block), so every slot holding that plugin
// shows it, also after a restart. An invalid image marks "looked, none on disk".
std::map<juce::String, juce::Image>& pluginGraphicMap() {
  static std::map<juce::String, juce::Image> images;
  return images;
}

juce::String pluginGraphicKey(const juce::String& pluginPath, const juce::String& name) {
  return pluginPath + "\n" + name;
}

juce::File pluginGraphicFile(const juce::String& key) {
#ifdef T3K_APP_DATA_DIR_NAME
  const char* dirName = T3K_APP_DATA_DIR_NAME;
#else
  const char* dirName = "TONE3000";
#endif
  auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  base = base.getChildFile("Application Support");
#endif
  // FNV-1a: a stable file name for an arbitrary path/URI.
  juce::uint64 hash = 14695981039346656037ull;
  for (auto c = key.getCharPointer(); !c.isEmpty(); ++c)
    hash = (hash ^ static_cast<juce::uint64>(*c)) * 1099511628211ull;
  return base.getChildFile(dirName).getChildFile("EffectGraphics")
      .getChildFile(juce::String::toHexString(static_cast<juce::int64>(hash)) + ".png");
}

// Holds a plugin's own editor at the size the plugin asks for. The window fits
// itself to this holder, but the holder never passes the window's size on to
// the editor: a window manager that re-adds its frame to the window size (KWin
// under XWayland) would otherwise stretch an embedded LV2 GUI, which resizes
// itself straight back, and the two fight hundreds of times a second (flicker).
class EditorHolder : public juce::Component {
public:
  explicit EditorHolder(juce::AudioProcessorEditor* editor) : editor_(editor) {
    addAndMakeVisible(*editor_);
    setSize(editor_->getWidth(), editor_->getHeight());
  }
  void childBoundsChanged(juce::Component* child) override {
    if (child == editor_.get())
      setSize(editor_->getWidth(), editor_->getHeight());
  }

private:
  std::unique_ptr<juce::AudioProcessorEditor> editor_;
};

// A top-level window hosting the plugin's own editor. Closing it deletes the
// window; the plugin instance is shared-owned, so the effect stays in the chain.
// After it has painted once it snapshots the editor as the slot's static graphic.
class HostedEffectWindow : public juce::DocumentWindow, private juce::Timer {
public:
  HostedEffectWindow(std::shared_ptr<juce::AudioPluginInstance> instance,
                     const juce::String& name, std::string blockId)
      : juce::DocumentWindow(name, juce::Colours::darkgrey,
                             juce::DocumentWindow::closeButton),
        instance_(std::move(instance)),
        blockId_(std::move(blockId)),
        pluginKey_(pluginGraphicKey(instance_->getPluginDescription().fileOrIdentifier, name)) {
    setUsingNativeTitleBar(true);
    if (auto* editor = instance_->createEditorAndMakeActive()) {
      setContentOwned(new EditorHolder(editor), true);
      centreWithSize(getWidth(), getHeight());
    } else {
      auto* generic = new juce::GenericAudioProcessorEditor(*instance_);
      setContentOwned(generic, true);
      centreWithSize(juce::jmax(400, generic->getWidth()), juce::jmax(300, generic->getHeight()));
    }
    setVisible(true);
    toFront(true);
    startTimer(400);  // capture after it has painted on screen
  }
  ~HostedEffectWindow() override {
    stopTimer();
    clearContentComponent();
  }
  // Deleted after this event has fully unwound: the close arrives from the
  // window's own peer, and the embedded plugin GUI may still have work queued.
  void closeButtonPressed() override {
    setVisible(false);
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::Component>(this)] {
      delete safe.getComponent();
    });
  }

private:
  void timerCallback() override {
    stopTimer();
    // Real on-screen pixels: a JUCE component snapshot is blank for native views.
    auto image = captureComponentWindow(*this);
    if (!image.isValid()) return;
    {
      const juce::ScopedLock sl(graphicLock());
      graphicMap()[blockId_] = image;
      pluginGraphicMap()[pluginKey_] = image;
    }
    const auto file = pluginGraphicFile(pluginKey_);
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream out(file);
    if (out.openedOk()) {
      out.setPosition(0);
      out.truncate();
      juce::PNGImageFormat().writeImageToStream(image, out);
    }
    graphicBroadcaster().sendChangeMessage();
  }

  std::shared_ptr<juce::AudioPluginInstance> instance_;
  std::string blockId_;
  juce::String pluginKey_;
};

}  // namespace

juce::Image hostedEffectGraphic(const std::string& blockId, const juce::String& pluginPath,
                               const juce::String& name) {
  const juce::ScopedLock sl(graphicLock());
  if (auto it = graphicMap().find(blockId); it != graphicMap().end())
    return it->second;
  const auto key = pluginGraphicKey(pluginPath, name);
  auto it = pluginGraphicMap().find(key);
  if (it == pluginGraphicMap().end())  // first ask for this plugin: read it from disk once
    it = pluginGraphicMap().emplace(key, juce::ImageFileFormat::loadFrom(pluginGraphicFile(key))).first;
  return it->second;
}

juce::ChangeBroadcaster& hostedEffectGraphics() { return graphicBroadcaster(); }

bool openHostedEffectEditor(Services& services, const std::string& blockId,
                            const juce::String& name) {
  auto instance = services.backend.hostedEffectInstance(blockId);
  if (instance == nullptr)
    return false;
  new HostedEffectWindow(std::move(instance), name, blockId);
  return true;
}

}  // namespace t3k::ui
