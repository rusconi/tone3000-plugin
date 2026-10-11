#include "Processor.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#if JUCE_PLUGINHOST_LV2
#include <lilv/lilv.h>  // the copy JUCE builds in (LV2_SDK), C linkage
#endif

namespace {

std::unique_ptr<juce::AudioPluginFormatManager> makeFormatManager() {
  auto formats = std::make_unique<juce::AudioPluginFormatManager>();
  juce::addDefaultFormatsToManager(*formats);
  return formats;
}

bool parseDescription(const juce::String& xml, juce::PluginDescription& description) {
  auto document = juce::XmlDocument::parse(xml);
  return document != nullptr && description.loadFromXml(*document) &&
         !description.isInstrument;
}

std::shared_ptr<juce::AudioPluginInstance> instantiateEffect(
    const juce::String& xml, const juce::MemoryBlock& state, double rate, int blockSize) {
  juce::PluginDescription description;
  if (!parseDescription(xml, description))
    return {};

  auto formats = makeFormatManager();
  juce::String error;
  auto instance = formats->createPluginInstance(description, rate, blockSize, error);
  if (instance == nullptr) {
    juce::Logger::writeToLog("[Hosted effect] " + description.name + ": " + error);
    return {};
  }
  if (instance->getTotalNumInputChannels() < 1 ||
      instance->getTotalNumInputChannels() > 2 ||
      instance->getTotalNumOutputChannels() < 1 ||
      instance->getTotalNumOutputChannels() > 2) {
    juce::Logger::writeToLog("[Hosted effect] Unsupported channel layout: " + description.name);
    return {};
  }
  instance->setRateAndBufferSizeDetails(rate, blockSize);
  instance->prepareToPlay(rate, blockSize);
  if (!state.isEmpty())
    instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  return std::shared_ptr<juce::AudioPluginInstance>(std::move(instance));
}

#if JUCE_MAC
// Reads the bundle's CFBundleIdentifier without loading/instantiating the plugin
// (CFBundleCreate just parses Info.plist — cheap and safe on the message thread).
juce::String bundleIdentifier(const juce::File& bundle) {
  const auto path = bundle.getFullPathName();
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(path.toRawUTF8()),
      static_cast<CFIndex>(path.getNumBytesAsUTF8()), true);
  if (url == nullptr)
    return {};
  CFBundleRef cfBundle = CFBundleCreate(kCFAllocatorDefault, url);
  CFRelease(url);
  if (cfBundle == nullptr)
    return {};
  juce::String result;
  if (CFStringRef id = CFBundleGetIdentifier(cfBundle)) {
    const CFIndex len = CFStringGetLength(id);
    const CFRange range = CFRangeMake(0, len);
    CFIndex used = 0;
    CFStringGetBytes(id, range, kCFStringEncodingUTF8, 0, false, nullptr, 0, &used);
    juce::HeapBlock<UInt8> buffer(static_cast<size_t>(used) + 1);
    CFStringGetBytes(id, range, kCFStringEncodingUTF8, 0, false,
                     reinterpret_cast<UInt8*>(buffer.getData()), used, nullptr);
    buffer[static_cast<size_t>(used)] = 0;
    result = juce::String::fromUTF8(reinterpret_cast<const char*>(buffer.getData()));
  }
  CFRelease(cfBundle);
  return result;
}
#endif

// Vendor label from a reverse-DNS bundle id, e.g. "com.fabfilter.Pro-Q.4" -> "FabFilter".
juce::String vendorFromBundleId(const juce::String& id) {
  if (id.isEmpty())
    return "Other";
  auto parts = juce::StringArray::fromTokens(id, ".", "");
  parts.trim();
  if (parts.isEmpty())
    return "Other";
  static const juce::StringArray kTlds{
      "com", "org", "net", "io", "co", "audio", "de", "se", "uk", "fr", "jp",
      "eu", "us", "me", "it", "nl", "ca", "ch", "at", "au", "tv", "app", "dev", "info", "biz"};
  int i = 0;
  while (i < parts.size() && kTlds.contains(parts[i].toLowerCase()))
    ++i;
  juce::String vendor = (i < parts.size()) ? parts[i] : parts.getReference(0);
  if (vendor.isEmpty())
    return "Other";
  // Normalise the grouping key (case + separators) so e.g. "plugin-alliance",
  // "pluginalliance" and "Pluginalliance" collapse to one vendor.
  const auto key = vendor.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
  // Display names for vendors whose reverse-DNS label is not their name.
  static const std::map<juce::String, juce::String> kAliases{
      {"uaudio", "Universal Audio"}, {"slatedigital", "Slate Digital"},
      {"pluginalliance", "Plugin Alliance"}, {"izotope", "iZotope"},
      {"fabfilter", "FabFilter"}, {"toneempire", "Tone Empire"},
      {"wavesaudio", "Waves"}, {"soundtheory", "Soundtheory"},
      {"bertomaudio", "Bertom Audio"}, {"ikmultimedia", "IK Multimedia"},
      {"nativeinstruments", "Native Instruments"}, {"neuraldsp", "Neural DSP"},
      {"blackroosteraudio", "Black Rooster Audio"}, {"unitedplugins", "United Plugins"},
      {"inmusicbrands", "inMusic Brands"}, {"theusualsuspects", "The Usual Suspects"},
      {"mcdsp", "McDSP"}, {"newfangledaudio", "Newfangled Audio"},
      {"lindellplugins", "Lindell Plugins"}, {"kiiveaudio", "Kiive Audio"},
      {"babyaudio", "BABY Audio"}, {"soundtoys", "Soundtoys"},
      {"softube", "Softube"}, {"eventide", "Eventide"}, {"ssl", "SSL"}};
  const auto alias = kAliases.find(key);
  if (alias != kAliases.end())
    return alias->second;
  return vendor.substring(0, 1).toUpperCase() + vendor.substring(1);
}

// Standard VST3 locations per the VST3 SDK spec. macOS keeps the single
// system-wide folder; Linux adds the per-user and both system folders.
juce::Array<juce::File> vst3SearchDirs() {
#if JUCE_MAC
  return {juce::File("/Library/Audio/Plug-Ins/VST3")};
#elif JUCE_LINUX || JUCE_BSD
  return {juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".vst3"),
          juce::File("/usr/lib/vst3"), juce::File("/usr/local/lib/vst3")};
#else
  return {};
#endif
}

// Calls visit() for each bundle directory matching wildcard under folder. On
// Linux plain subfolders are walked too (e.g. yabridge's ~/.vst3/yabridge/),
// but never the inside of a bundle.
template <typename Visit>
void forEachBundle(const juce::File& folder, const juce::String& wildcard, Visit&& visit,
                   int depth = 0) {
  if (!folder.isDirectory())
    return;
  for (const auto& entry : juce::RangedDirectoryIterator(
           folder, /*recursive*/ false, "*", juce::File::findDirectories)) {
    const auto file = entry.getFile();
    if (file.getFileName().matchesWildcard(wildcard, true))
      visit(file);
#if !JUCE_MAC
    else if (depth < 2)
      forEachBundle(file, wildcard, visit, depth + 1);
#endif
  }
}

#if !JUCE_MAC
// Factory vendor from a VST3 bundle's Contents/Resources/moduleinfo.json, read
// as text (the file is JSON5: trailing commas, which juce::JSON rejects).
juce::String vst3ModuleInfoVendor(const juce::File& bundle) {
  const auto text = bundle.getChildFile("Contents/Resources/moduleinfo.json").loadFileAsString();
  const int factory = text.indexOf("\"Factory Info\"");
  if (factory < 0)
    return {};
  const int key = text.indexOf(factory, "\"Vendor\"");
  const int colon = key < 0 ? -1 : text.indexOfChar(key + 8, ':');
  const int open = colon < 0 ? -1 : text.indexOfChar(colon, '"');
  const int close = open < 0 ? -1 : text.indexOfChar(open + 1, '"');
  return close < 0 ? juce::String() : text.substring(open + 1, close).trim();
}
#endif

// One add-effect menu row. `path` is what addHostedEffectFromFile() takes: the
// bundle path for AU / VST3, the plugin URI for LV2 (a bundle can hold several).
struct EffectEntry {
  juce::String name, format, path, vendor;
};

struct InstalledBundle {
  juce::File file;
  juce::String format;  // "AudioUnit", "VST3" or "LV2"
};

#if JUCE_PLUGINHOST_LV2
// LV2_PATH if set, else the standard per-user and system folders.
juce::Array<juce::File> lv2SearchDirs() {
  const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  auto path = juce::SystemStats::getEnvironmentVariable("LV2_PATH", {});
  if (path.isEmpty())
    return {home.getChildFile(".lv2"), juce::File("/usr/local/lib/lv2"), juce::File("/usr/lib/lv2")};
  juce::Array<juce::File> dirs;
  for (auto dir : juce::StringArray::fromTokens(path, ":", ""))
    if ((dir = dir.trim()).isNotEmpty())
      dirs.add(dir.startsWithChar('~') ? home.getChildFile(dir.substring(2)) : juce::File(dir));
  return dirs;
}

// The effects in the given LV2 bundles, straight from lilv: only the Turtle
// metadata is read, no plugin binary is loaded. (JUCE's LV2 format reloads
// every installed bundle on each query: slow, and lilv warns on each reload.)
std::map<juce::String, std::vector<EffectEntry>> scanLv2Bundles(const std::vector<juce::File>& bundles) {
  std::map<juce::String, std::vector<EffectEntry>> result;
  if (bundles.empty())
    return result;
  LilvWorld* world = lilv_world_new();
  if (world == nullptr)
    return result;
  for (const auto& bundle : bundles) {
    result[bundle.getFullPathName()];  // a bundle with no effects is cached as such
    LilvNode* uri = lilv_new_file_uri(world, nullptr, (bundle.getFullPathName() + "/").toRawUTF8());
    lilv_world_load_bundle(world, uri);
    lilv_node_free(uri);
  }
  LilvNode* rdfType = lilv_new_uri(world, LILV_NS_RDF "type");
  LilvNode* instrument = lilv_new_uri(world, LV2_CORE__InstrumentPlugin);
  const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
  LILV_FOREACH(plugins, it, plugins) {
    const LilvPlugin* plugin = lilv_plugins_get(plugins, it);
    LilvNode* name = lilv_plugin_get_name(plugin);  // also loads the plugin's data
    LilvNode* author = lilv_plugin_get_author_name(plugin);
    const LilvNode* pluginUri = lilv_plugin_get_uri(plugin);
    // Asked directly: the lv2core class tree is not loaded here.
    if (!lilv_world_ask(world, pluginUri, rdfType, instrument)) {
      char* bundlePath = lilv_node_get_path(lilv_plugin_get_bundle_uri(plugin), nullptr);
      if (bundlePath != nullptr) {
        result[juce::File(juce::String::fromUTF8(bundlePath)).getFullPathName()].push_back(
            {juce::String::fromUTF8(name != nullptr ? lilv_node_as_string(name) : ""), "LV2",
             juce::String::fromUTF8(lilv_node_as_uri(pluginUri)),
             author != nullptr ? juce::String::fromUTF8(lilv_node_as_string(author)) : juce::String("Other")});
        lilv_free(bundlePath);
      }
    }
    lilv_node_free(name);
    lilv_node_free(author);
  }
  lilv_node_free(instrument);
  lilv_node_free(rdfType);
  lilv_world_free(world);
  return result;
}
#endif

// Changes whenever a plugin in the bundle is (re)installed or updated: the
// bundle folder itself plus the metadata a host reads (LV2 Turtle files; VST3
// moduleinfo.json, the per-arch binary folders under Contents; Info.plist).
// Order-independent sum, as directory listing order is not fixed.
juce::String bundleStamp(const juce::File& bundle) {
  juce::uint64 sum = 0;
  int count = 0;
  auto add = [&](const juce::File& f) {
    const auto t = static_cast<juce::uint64>(f.getLastModificationTime().toMilliseconds());
    sum += (t * 1000003ull) ^ static_cast<juce::uint64>(f.getSize());
    ++count;
  };
  add(bundle);
  for (const auto& entry : juce::RangedDirectoryIterator(bundle, false, "*.ttl", juce::File::findFiles))
    add(entry.getFile());
  for (const auto* relative : {"Contents", "Contents/Resources/moduleinfo.json", "Contents/Info.plist"})
    if (const auto f = bundle.getChildFile(relative); f.exists())
      add(f);
  return juce::String::toHexString(static_cast<juce::int64>(sum)) + "/" + juce::String(count);
}

// The add-effect menu's listing, cached like a DAW's plugin scan: per bundle,
// keyed by bundleStamp(), so opening the menu only walks the plugin folders
// and re-reads just the bundles that are new or changed. Removed bundles drop
// out. Persisted as JSON in the app data folder, shared by all instances.
class EffectListingCache {
public:
  static EffectListingCache& instance() {
    static EffectListingCache cache;
    return cache;
  }

  juce::Array<juce::var> list(const std::vector<InstalledBundle>& bundles) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_)
      load();

    std::map<juce::String, Record> current;
    std::vector<juce::File> staleLv2;
    bool changed = bundles.size() != records_.size();
    for (const auto& bundle : bundles) {
      const auto path = bundle.file.getFullPathName();
      const auto stamp = bundleStamp(bundle.file);
      if (auto it = records_.find(path); it != records_.end() && it->second.stamp == stamp) {
        current[path] = it->second;
        continue;
      }
      changed = true;
      Record record{stamp, {}};
      if (bundle.format == "LV2")
        staleLv2.push_back(bundle.file);
      else
        record.entries.push_back({bundle.file.getFileNameWithoutExtension(), bundle.format, path,
                                  TONE3000Processor::vendorForBundle(path)});
      current[path] = std::move(record);
    }
#if JUCE_PLUGINHOST_LV2
    for (auto& [path, entries] : scanLv2Bundles(staleLv2))
      if (auto it = current.find(path); it != current.end())
        it->second.entries = std::move(entries);
#endif
    records_ = std::move(current);
    if (changed)
      save();

    // Folder order (user folders first), and an LV2 URI found in two folders
    // only once, the first one, as lilv itself resolves it.
    juce::Array<juce::var> result;
    juce::StringArray seen;
    for (const auto& bundle : bundles)
      for (const auto& entry : records_[bundle.file.getFullPathName()].entries) {
        if (seen.contains(entry.path))
          continue;
        seen.add(entry.path);
        auto* item = new juce::DynamicObject();
        item->setProperty("name", entry.name);
        item->setProperty("format", entry.format);
        item->setProperty("path", entry.path);
        item->setProperty("vendor", entry.vendor);
        result.add(juce::var(item));
      }
    return result;
  }

private:
  struct Record {
    juce::String stamp;
    std::vector<EffectEntry> entries;
  };

  static juce::File file() {
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile(T3K_APP_DATA_DIR_NAME).getChildFile("HostedEffectsCache.json");
  }

  void load() {
    loaded_ = true;
    const auto json = juce::JSON::parse(file());
    if (static_cast<int>(json["version"]) != kVersion)
      return;  // missing, unreadable or an older layout: rescan everything
    if (auto* bundles = json["bundles"].getDynamicObject())
      for (const auto& property : bundles->getProperties()) {
        Record record{property.value["stamp"].toString(), {}};
        if (auto* entries = property.value["entries"].getArray())
          for (const auto& e : *entries)
            record.entries.push_back({e["name"].toString(), e["format"].toString(), e["path"].toString(),
                                      e["vendor"].toString()});
        records_[property.name.toString()] = std::move(record);
      }
  }

  void save() const {
    auto* bundles = new juce::DynamicObject();
    for (const auto& [path, record] : records_) {
      juce::Array<juce::var> entries;
      for (const auto& e : record.entries) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("name", e.name);
        entry->setProperty("format", e.format);
        entry->setProperty("path", e.path);
        entry->setProperty("vendor", e.vendor);
        entries.add(juce::var(entry));
      }
      auto* bundle = new juce::DynamicObject();
      bundle->setProperty("stamp", record.stamp);
      bundle->setProperty("entries", entries);
      bundles->setProperty(path, juce::var(bundle));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("version", kVersion);
    root->setProperty("bundles", juce::var(bundles));

    const auto target = file();
    target.getParentDirectory().createDirectory();
    // Written aside and swapped in, so a concurrent instance never reads half a file.
    juce::TemporaryFile temp(target);
    if (temp.getFile().replaceWithText(juce::JSON::toString(juce::var(root))))
      temp.overwriteTargetFileWithTemporary();
  }

  static constexpr int kVersion = 1;
  std::mutex mutex_;
  bool loaded_ = false;
  std::map<juce::String, Record> records_;
};

}  // namespace

juce::String TONE3000Processor::vendorForBundle(const juce::String& bundlePath) {
#if JUCE_MAC
  return vendorFromBundleId(bundleIdentifier(juce::File(bundlePath)));
#else
  if (juce::File::isAbsolutePath(bundlePath) && bundlePath.endsWithIgnoreCase(".vst3"))
    if (auto vendor = vst3ModuleInfoVendor(juce::File(bundlePath)); vendor.isNotEmpty())
      return vendor;
  return "Other";
#endif
}

juce::var TONE3000Processor::listHostedEffectBundles() const {
  // Only the folders are walked here; EffectListingCache reads the bundles.
  std::vector<InstalledBundle> bundles;
  auto collect = [&bundles](const juce::File& folder, const juce::String& wildcard,
                            const juce::String& format) {
    forEachBundle(folder, wildcard, [&](const juce::File& bundle) { bundles.push_back({bundle, format}); });
  };
#if JUCE_MAC
  collect(juce::File("/Library/Audio/Plug-Ins/Components"), "*.component", "AudioUnit");
#endif
  for (const auto& dir : vst3SearchDirs())
    collect(dir, "*.vst3", "VST3");
#if JUCE_PLUGINHOST_LV2
  // As lilv loads them: every folder directly in an LV2 path entry is a bundle.
  for (const auto& dir : lv2SearchDirs())
    if (dir.isDirectory())
      for (const auto& entry : juce::RangedDirectoryIterator(dir, false, "*", juce::File::findDirectories))
        if (entry.getFile().getChildFile("manifest.ttl").existsAsFile())
          bundles.push_back({entry.getFile(), "LV2"});
#endif
  return EffectListingCache::instance().list(bundles);
}

juce::var TONE3000Processor::listHostedEffects() const {
  juce::Array<juce::var> result;
  auto formats = makeFormatManager();
  for (int i = 0; i < formats->getNumFormats(); ++i) {
    auto* format = formats->getFormat(i);
    if (format == nullptr || (format->getName() != "AudioUnit" && format->getName() != "VST3" &&
                              format->getName() != "LV2"))
      continue;
    // Hardcoded standard locations per format instead of every default location
    // JUCE would otherwise walk. Predictable and fast.
    juce::FileSearchPath searchPaths;
    if (format->getName() == "AudioUnit")
      searchPaths.add(juce::File("/Library/Audio/Plug-Ins/Components"));
    else if (format->getName() == "VST3")
      for (const auto& dir : vst3SearchDirs())
        searchPaths.add(dir);
    else if (format->getName() == "LV2")
      searchPaths = format->getDefaultLocationsToSearch();
#if JUCE_MAC
    const bool recursive = false;
#else
    const bool recursive = true;  // e.g. yabridge's ~/.vst3/yabridge/
#endif
    auto identifiers = format->searchPathsForPlugins(searchPaths, recursive, false);
    for (const auto& identifier : identifiers) {
      juce::OwnedArray<juce::PluginDescription> descriptions;
      format->findAllTypesForFile(descriptions, identifier);
      for (const auto* description : descriptions) {
        if (description == nullptr || description->isInstrument ||
            description->name.containsIgnoreCase("TONE3000"))
          continue;
        auto xml = description->createXml();
        if (xml == nullptr)
          continue;
        auto* item = new juce::DynamicObject();
        item->setProperty("name", description->name);
        item->setProperty("manufacturer", description->manufacturerName);
        item->setProperty("format", format->getName());
        item->setProperty("description", xml->toString());
        result.add(juce::var(item));
      }
    }
  }
  return result;
}

std::string TONE3000Processor::addHostedEffect(const juce::String& xml,
                                                const std::string& targetInsertId) {
  juce::PluginDescription description;
  if (!parseDescription(xml, description))
    return {};
  auto instance = instantiateEffect(xml, {}, chainSampleRate(), chainDomainBlockSize());
  if (instance == nullptr)
    return {};

  ChainEditFade editFade(*this);
  juce::ScopedLock lock(chainMutex);
  pushChainHistory();
  const std::string id = juce::Uuid().toString().toStdString();
  auto block = std::make_unique<ChainBlock>(id, ChainBlockType::EFFECT);
  block->effectDescriptionXml = xml;
  block->effectName = description.name;
  block->effectInstance = std::move(instance);
  block->loaded = true;
  block->effectScratch.setSize(juce::jmax(block->effectInstance->getTotalNumInputChannels(),
                                         block->effectInstance->getTotalNumOutputChannels()),
                               chainDomainBlockSize());
  prepareBlockForChainRate(*block);

  Lane* target = nullptr;
  Lane::iterator slot;
  for (auto& laneBlocks : lanes) {
    auto it = std::find_if(laneBlocks.begin(), laneBlocks.end(), [&](const auto& candidate) {
      return candidate->type == ChainBlockType::INSERT && candidate->id == targetInsertId;
    });
    if (it != laneBlocks.end()) {
      target = &laneBlocks;
      slot = it;
      break;
    }
  }
  if (target == nullptr) {
    target = &activeChain();
    slot = std::find_if(target->begin(), target->end(), [](const auto& candidate) {
      return candidate->type == ChainBlockType::INSERT;
    });
  }
  if (slot != target->end())
    *slot = std::move(block);
  else
    target->push_back(std::move(block));
  alignBranchLaneLengths();
  bumpChainRevision();
  triggerAsyncUpdate();
  return id;
}

std::string TONE3000Processor::addHostedEffectFromFile(
    const juce::String& fileOrIdentifier, const std::string& targetInsertId) {
  // A bundle path (.component / .vst3 / .lv2) or an LV2 plugin URI.
  const bool isPath = juce::File::isAbsolutePath(fileOrIdentifier);
  const auto formatName = !isPath                                        ? juce::String("LV2")
                          : fileOrIdentifier.endsWithIgnoreCase(".component") ? juce::String("AudioUnit")
                          : fileOrIdentifier.endsWithIgnoreCase(".vst3")      ? juce::String("VST3")
                          : fileOrIdentifier.endsWithIgnoreCase(".lv2")       ? juce::String("LV2")
                                                                              : juce::String();
  if (formatName.isEmpty())
    return {};
  auto formats = makeFormatManager();
  for (int i = 0; i < formats->getNumFormats(); ++i) {
    auto* format = formats->getFormat(i);
    if (format == nullptr || format->getName() != formatName)
      continue;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format->findAllTypesForFile(descriptions, fileOrIdentifier);
    for (const auto* description : descriptions) {
      if (description == nullptr || description->isInstrument)
        continue;
      if (auto xml = description->createXml())
        return addHostedEffect(xml->toString(), targetInsertId);
    }
  }
  return {};
}

void TONE3000Processor::restoreHostedEffect(ChainBlock& block) {
  block.effectInstance = instantiateEffect(block.effectDescriptionXml, block.effectState,
                                            chainSampleRate(), chainDomainBlockSize());
  block.loaded = block.effectInstance != nullptr;
  if (block.loaded)
    block.effectScratch.setSize(juce::jmax(block.effectInstance->getTotalNumInputChannels(),
                                          block.effectInstance->getTotalNumOutputChannels()),
                                chainDomainBlockSize());
  block.loadFailed = !block.loaded;
  block.modelLoading = false;
}

std::shared_ptr<juce::AudioPluginInstance> TONE3000Processor::hostedEffectInstance(
    const std::string& blockId) {
  juce::ScopedLock lock(chainMutex);
  auto* block = findBlockById(blockId);
  return block != nullptr && block->type == ChainBlockType::EFFECT
             ? block->effectInstance : std::shared_ptr<juce::AudioPluginInstance>{};
}
