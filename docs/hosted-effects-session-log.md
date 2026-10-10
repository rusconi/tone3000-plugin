# Hosted AU/VST3 effects — session log

**What this is:** a complete record of the work that added **hosted AU/VST3 effects**
(Effect blocks in the chain) to the TONE3000 fork, including a first-class add-effect
browser. Written so a future session — Codex or otherwise — can pick it up with zero
archaeology.

- Fork: `zack/tone3000-plugin-effects` (Gitea) — fork of `github.com/tone-3000/tone3000-plugin`, base `ab5e5ae`, v0.0.12.
- Working clone: `/Users/zack/.codex/.tmp/tone3000-build.7xAQeo/repo`
- Handoff (state + how it works): `docs/hosted-effects-handoff.md`
- User-facing usage: `docs/hosted-effects.md`
- Status: **feature complete, validated, and confirmed working by the user.**

## Commits (oldest → newest)

| commit | what |
|---|---|
| `7ce1aea` | **Core feature** — Effect block type: list/add/instantiate AU+VST3 effects, process via a preallocated scratch buffer, round-trip description XML + state through presets/DAW projects; AddTile menu + ToneTile effect window. (Started by Codex; committed here.) |
| `6ea47f3` | docs: handoff — push confirmed + artifact package path |
| `03c6a06` | docs: build-time `T3K_PUBLISHABLE_KEY` + pre-existing `DspTests` link note |
| `25ba029` | docs: pluginval 1.0.4 SUCCESS for VST3 + AU |
| `3966edc` | Hardcode scan paths → `/Library/Audio/Plug-Ins/{Components,VST3}` |
| `93cbaa7` | First menu attempt: `KnownPluginList` + manufacturer submenus (native PopupMenu) |
| `3c62e18` | **Fix hang** — menu hung; list bundles from disk instead of the blocking scan |
| `d3dce6d` | Group the menu by **vendor** (read from bundle id; no scan) |
| `8f37895` | **Fix macOS submenu z-order** (JUCE 9.0.2+ regression) + reproducible patch script |
| `496e682` | **In-app browser with a blurred background menu** (`EffectMenu`) |
| `956e8fa` | Centre the browser panel and clamp it to the window |

22 files changed vs base (`+1060 / −10`).

## How the feature works

- **Effect block** (`ChainBlockType::EFFECT`): a chain slot that hosts an installed macOS
  **AU or VST3 effect** (JUCE `PLUGINHOST_AU` + `JUCE_PLUGINHOST_VST3`). Processed through a
  preallocated scratch buffer so the audio callback never allocates. Serial/parallel latency
  is reported to the host. Description XML + opaque state are serialized, so presets and DAW
  projects restore the effect; a missing plug-in stays as a bypassed block.
- **Add-effect browser** (`plugin/ui/widgets/EffectMenu.{h,cpp}`, a `Popover`): two columns —
  **vendors** left, that vendor's effects right. Picking a vendor snapshots and **blurs the
  vendor column** (`core/Blur.h` `blurImage`) so the background menu recedes. The panel is
  **centred over the tile and clamped inside the window**, and keeps its centre when it widens.
- **Where the list comes from** (`TONE3000Processor::listHostedEffectBundles()`): a plain
  **directory listing** of `/Library/Audio/Plug-Ins/Components` (`*.component`) and
  `/Library/Audio/Plug-Ins/VST3` (`*.vst3`) — no scan, no instantiation (see gotcha #1).
  Each entry's **vendor** comes from the bundle's reverse-DNS `CFBundleIdentifier`
  (`CFBundleGetIdentifier`, CoreFoundation, no instantiation), normalised so
  `plugin-alliance`/`pluginalliance` merge; a small alias map fixes display names.

## Build & reproduce

Self-contained toolchain lives in the build root (not on PATH):

```
ROOT=/Users/zack/.codex/.tmp/tone3000-build.7xAQeo
CM=$ROOT/tools/cmake/data/bin/cmake
REPO=$ROOT/repo

# 1) publishable key (configure-time; repo/.env is gitignored)
#    T3K_PUBLISHABLE_KEY=...   (see docs/hosted-effects-handoff.md — recovered from the app)
# 2) macOS popup-submenu fix (applied to the fetched JUCE; reapply after a clean configure)
sh $REPO/script/patch_juce_popupmenu.sh
# 3) configure + build ONLY the plugin targets
"$CM" -S "$REPO" -B "$REPO/build"
"$CM" --build "$REPO/build" --target TONE3000_Standalone TONE3000_AU TONE3000_VST3 -j8
```

Artifacts: `$REPO/build/plugin/TONE3000_artefacts/Release/{Standalone,AU,VST3}/`.
Package: `$ROOT/dist/TONE3000-effects-0.0.12-macos-arm64-2026-10-10.zip`.
Installed for testing: `~/Library/Audio/Plug-Ins/{Components,VST3}/` and `~/Desktop/TONE3000.app`.

> The `DspTests` target fails to link (`Undefined symbols … arm64`) — **pre-existing**, unrelated;
> build the three plugin targets only.

## Validation

| check | result |
|---|---|
| Compile (Standalone + AU + VST3) | PASS |
| `auval -v aumf T3K0 TFTF` | AU VALIDATION SUCCEEDED |
| `pluginval --strictness-level 8` VST3 | SUCCESS |
| `pluginval --strictness-level 8` AU | SUCCESS |
| Standalone launch | runs, quits clean, no crash |
| Add-effect browser | **confirmed working by the user** (vendor grouping, blurred background, centred) |

## Gotchas hit (so they don't bite again)

1. **Menu hang** — `AudioPluginFormat::searchPathsForPlugins` *instantiates every plugin* to
   identify it; called on the message thread it froze the UI (1024 AU + 140 VST3). Never call
   it on the message thread; list the bundle folders instead.
2. **macOS popup submenu z-order** — JUCE 9.0.2+ regression (upstream `5fc8d348`): submenus
   render behind the parent. `script/patch_juce_popupmenu.sh` skips `windowIsTemporary` windows.
   (Moot for the add-effect browser now — it has no native submenus.)
3. **JUCE API details** — `searchPathsForPlugins` takes a `FileSearchPath` (not `StringArray`)
   and `add()` takes a `File`; `juce::Array` uses `isEmpty()` (not `empty()`).
4. **Native popups can't blur their parent** — hence the in-app `EffectMenu`.
5. **Publishable key** — empty → sign-in error; must be set at *configure* time.
6. **Shallow clone** — Gitea rejects shallow pushes; `git fetch --unshallow` first.
7. **Edit tooling** — a written `.cpp` lost its trailing namespace brace once; watch for
   "expected '}'" after writing new files.

## Still open (not blocking)

- Out-of-process scan + cached `KnownPluginList` (Element's model) for marketing-grade vendor
  names and a "scan for new plugins" button.
- Validate parallel-branch latency when a hosted effect changes its own latency.
- Exact marketing vendor names for a few smaller developers (extend the alias map in
  `ProcessorHostedEffects.cpp`).
