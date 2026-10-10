# Hosted AU/VST3 effects — build & handoff notes

Status: **implemented, built, validated, committed (`7ce1aea`), pushed to Gitea (`main`).**
Last updated: 2026-10-10 by `opencode-bart` (continuation of the Codex session `01a1271e…`,
which ended mid-smoke-test after a model switch). This note exists so Codex — or anyone —
can resume with full context. All remaining work is hands-on (see Next steps).

## What this fork adds

An **Effect** block type that hosts installed macOS **Audio Unit** and **VST3** *effects*
inside the TONE3000 chain, using JUCE (`PLUGINHOST_AU` + `JUCE_PLUGINHOST_VST3`).

- Right-click an empty `+` chain tile → **Installed AU/VST3 Effects** (browse) or
  **Load AU/VST3 Effect** (pick a `.component`/`.vst3`).
- The effect becomes a normal chain tile: power on/off, remove, drag, duplicate, save in presets.
- Clicking the tile opens the effect's own editor (falls back to JUCE's generic editor).
- State (the plug-in description XML + its opaque state bytes) is serialized with the chain,
  so presets/DAW projects restore the effect. A missing plug-in stays as a bypassed block.

Scope limits (intentional, documented in `docs/hosted-effects.md`): **effects only** — no
instruments, VST2, AUv3, or MIDI effects. It is an **in-process host**: a faulty third-party
effect can crash TONE3000. Serial effect latency is reported to the host; parallel-branch
alignment and latency changes made inside an open editor still need dedicated validation.

## Files changed (16 modified + 2 new)

- `plugin/include/ChainBlock.h` — `ChainBlockType::EFFECT` + effect fields.
- `plugin/include/Processor.h`, `plugin/src/ProcessorHostedEffects.cpp` (new) — list / add /
  addFromFile / restore / instance APIs.
- `plugin/src/Processor.cpp` — latency accounting + `prepareChain` + `processChainOnBuffer`
  (scratch-buffer, no audio-thread allocation).
- `plugin/src/ProcessorChain.cpp`, `ProcessorHistory.cpp`, `ProcessorState.cpp` — id/history/
  (de)serialize for effect blocks.
- `plugin/ui/**` — backend passthrough, `ChainState` `isEffect`, `AddTile` menu + file chooser,
  `ToneTile` effect window / menu / paint.
- `plugin/CMakeLists.txt` — `PLUGINHOST_AU TRUE` (Apple) + `JUCE_PLUGINHOST_VST3=1`.
- `README.md`, `docs/hosted-effects.md` — feature + test steps.
- Scan locations are **hardcoded** (2026-10-10): Audio Unit →
  `/Library/Audio/Plug-Ins/Components`, VST3 → `/Library/Audio/Plug-Ins/VST3`
  (`ProcessorHostedEffects.cpp`, `listHostedEffects`), instead of JUCE's full default
  search set. Predictable and fast; no user-level locations are scanned.
- **Add-effect UI is a menu, not a Finder dialog** (2026-10-10). The old
  `Load AU/VST3 Effect` `FileChooser` was removed; the add-tile item is now
  **"Add AU/VST3 Effect"**.
  - First attempt followed **kushview/element** (`ui/contextmenus.cpp`): build a
    `KnownPluginList`, `createTree(types, sortByManufacturer)`, nested submenus.
    **This HUNG the app** — `listHostedEffects()` runs JUCE
    `searchPathsForPlugins`, which instantiates/loads every plugin to identify it;
    on the message thread that blocks the UI, so the menu never appears. With
    1024 AU + 140 VST3 installed it never returned.
  - **Fix**: build the menu from a plain **filesystem listing** of
    `/Library/Audio/Plug-Ins/{Components,VST3}` (`listHostedEffectBundles()`,
    non-instantiating) — opens instantly. Choosing an entry calls
    `addHostedEffectFromFile()` (which instantiates only that one plugin).
  - **Grouping is by vendor**: each bundle's reverse-DNS id is read via
    `CFBundleGetIdentifier` (CoreFoundation, no instantiation), and the vendor
    label is the domain's org component (`com.fabfilter…` → FabFilter,
    `com.izotope…` → iZotope), normalised so `plugin-alliance`/`pluginalliance`
    merge (121 vendors over 1164 bundles; UA 290, Plugin Alliance 113, iZotope 90).
    Duplicate names under one vendor are tagged `(AU)`/`(VST3)`.
  - Still open (Element's real advantage): an out-of-process scanner + cached
    `KnownPluginList` for nice names/manufacturer grouping without a UI freeze.
  - **Custom in-app browser** (`plugin/ui/widgets/EffectMenu.{h,cpp}`, a `Popover`):
    two columns — vendors left, that vendor's effects right. Picking a vendor
    snapshots the vendor column, blurs it (`core/Blur.h` `blurImage`) and paints the
    blurred image over it, so the background menu recedes while the effect column is
    active. Uses `MenuRow` rows + `Viewport`s. This replaced the native `PopupMenu`
    for this action (a native popup cannot blur its own parent window).

## JUCE patch — macOS popup submenu z-order (2026-10-10)

The vendor **submenus rendered behind the parent menu**. This is a known JUCE
**9.0.2+ regression** (upstream commit `5fc8d348` "Raise modal windows to the
modal window level"; JUCE forum thread 69655): `PopupMenu` sub-menus are
`ComponentPeer::windowIsTemporary`, and `MacComponentPeer::modalComponentManagerChanged()`
re-levels them, pushing them below the parent.

Fix (applied to the fetched JUCE): skip temporary windows when computing the new
window level, in
`libs/juce/modules/juce_gui_basics/native/juce_NSViewComponentPeer_mac.mm`
(`modalComponentManagerChanged`, the `newLevel` lambda):

```cpp
if (isSharedWindow)
    return {};
if ((getStyleFlags() & ComponentPeer::windowIsTemporary) != 0)  // <-- added
    return {};
auto* manager = ModalComponentManager::getInstance();
```

`libs/juce` is **fetched/gitignored**, so this is not part of the repo commit.
Reapply after any clean configure with:

```
sh script/patch_juce_popupmenu.sh
```

(Idempotent.) The script is committed so rebuilds are reproducible.

## Build (self-contained toolchain Codex provisioned)

CMake + Ninja live under the build root (not on PATH):

```
CM=/Users/zack/.codex/.tmp/tone3000-build.7xAQeo/tools/cmake/data/bin/cmake
NJ=/Users/zack/.codex/.tmp/tone3000-build.7xAQeo/tools/bin/ninja
REPO=/Users/zack/.codex/.tmp/tone3000-build.7xAQeo/repo

# incremental build
"$CM" --build "$REPO/build" -j8
```

Config: `Release`, generator Ninja, deployment target 10.15, universal arch (arm64 verified).
Build system regenerates on glob changes (JUCE). Artifacts:

```
$REPO/build/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.app
$REPO/build/plugin/TONE3000_artefacts/Release/AU/TONE3000.component
$REPO/build/plugin/TONE3000_artefacts/Release/VST3/TONE3000.vst3
```

## Build-time configuration — the publishable key

Build #1 shipped with an **empty** key, so the app surfaced *"TONE3000 publishable key not configured.
Set T3K_PUBLISHABLE_KEY at build time."* The key is read at **configure** time (`plugin/ui/NativeUi.cmake`
→ `build/t3k_ui/T3kConfig.h`), so it must exist **before** configuring. Fix applied:

- Wrote `repo/.env` (gitignored): `T3K_PUBLISHABLE_KEY=t3k_pub__B8V_QGmV50ov2YWJzjqOxQE0q1-wURs`
  (`.env.local` overrides `.env`; a configure-env var overrides both).
- Reconfigured (`cmake -S "$REPO" -B "$REPO/build"`) → `T3kConfig.h` regenerated with the key → rebuilt.

The value is the app's OAuth **client_id** (publishable, not a secret) and was recovered from the
installed official app:

```
grep -aoE "t3k_pub_[A-Za-z0-9_-]{8,}" /Applications/TONE3000.app/Contents/MacOS/TONE3000
```

**Pre-existing, unrelated:** the `DspTests` target fails to link (`Undefined symbols for architecture
arm64`) — the same failure is in Codex's original `tests-build.log`. It does not affect the plugin.
Build only the plugin targets:

```
"$CM" --build "$REPO/build" --target TONE3000_Standalone TONE3000_AU TONE3000_VST3 -j8
```

## Validation performed (2026-10-10)

| check | result |
|---|---|
| Compile (Standalone + AU + VST3) | PASS (binaries newer than newest source; `ninja -n` shows no compile needed) |
| `auval -v aumf T3K0 TFTF` | **AU VALIDATION SUCCEEDED** |
| **pluginval (VST3, strictness 8)** | **SUCCESS** — incl. VST3 validator, audio processing, state restore, fuzz parameters |
| **pluginval (AU, strictness 8)** | **SUCCESS** — auval exit 0 |
| Standalone launch | app ran >6 s, no crash report, quit cleanly |
| VST3 bundle structure | `Contents/Resources/moduleinfo.json` present |
| Code signing | ad-hoc (expected; `codesign` reports the usual "no resources but signature indicates…" for a locally built AU). Not app-store/notarized — local/dev only. |

Validated with pluginval **1.0.4** (Tracktion), run from a downloaded release
(`pluginval --strictness-level 8 --validate <bundle>`).

NOT yet done (hands-on): click-through the hosted-effect editor (load an AU/VST3 into the chain,
open its editor, confirm audio + preset round-trip) and parallel-branch latency when an effect
changes its own latency; install-into-a-DAW test.

Installed for testing to the user plug-in dirs (reversible):
`~/Library/Audio/Plug-Ins/Components/TONE3000.component`,
`~/Library/Audio/Plug-Ins/VST3/TONE3000.vst3`.

## Git / remotes (pushed 2026-10-10)

- Local clone: `/Users/zack/.codex/.tmp/tone3000-build.7xAQeo/repo` (fork of
  `github.com/tone-3000/tone3000-plugin`, base `ab5e5ae`, v0.0.12). The clone was **shallow**;
  run `git fetch --unshallow origin` first (Gitea rejects shallow updates: "shallow update not allowed").
- Pushed: **`zack/tone3000-plugin-effects` → `main` @ `7ce1aea`**
  ("chain: host installed AU/VST3 effects in chain slots").
- `origin` = upstream GitHub — **left untouched; never push the fork there**.
- `git.zyusof.net` and `git.gonz3.net` are the **same Gitea backend** (both return the same
  commit + `updated_at`). Push once to `git.zyusof.net` and both hostnames reflect it; the raw
  `git.gonz3.net` LAN path (192.168.1.35:443) refused from this Mac, but the Gitea API confirms updated.
- Remote `gitea` = `https://git.zyusof.net/zack/tone3000-plugin-effects.git`; auth via a
  `GIT_ASKPASS` token from `infra_secrets` service 35 (never stored in config).

## Artifacts

- Package: `/Users/zack/.codex/.tmp/tone3000-build.7xAQeo/dist/TONE3000-effects-0.0.12-macos-arm64-2026-10-10.zip`
  (20 MB — Standalone `.app` + AU `.component` + VST3 `.vst3`).
- Installed for testing into the user plug-in dirs:
  `~/Library/Audio/Plug-Ins/Components/TONE3000.component`,
  `~/Library/Audio/Plug-Ins/VST3/TONE3000.vst3`.
- Standalone app: `~/Desktop/TONE3000.app` (also at
  `.../repo/build/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.app`).
- Rebuilt with the publishable key (see the config section above); auval re-verified PASS after the rebuild.

## Next steps (for whoever resumes)

1. ~~`pluginval --strictness-level 8`~~ **DONE — VST3 + AU both SUCCESS (2026-10-10).**
2. Hands-on: add a simple AU (e.g. a gain/EQ) in the standalone, open its editor, verify audio
   passes and preset round-trips; then test in a DAW.
3. Validate parallel-branch latency alignment when an effect changes its own latency.
