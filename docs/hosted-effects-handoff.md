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

## Validation performed (2026-10-10)

| check | result |
|---|---|
| Compile (Standalone + AU + VST3) | PASS (binaries newer than newest source; `ninja -n` shows no compile needed) |
| `auval -v aumf T3K0 TFTF` | **AU VALIDATION SUCCEEDED** |
| Standalone launch | app ran >6 s, no crash report, quit cleanly |
| VST3 bundle structure | `Contents/Resources/moduleinfo.json` present |
| Code signing | ad-hoc (expected; `codesign` reports the usual "no resources but signature indicates…" for a locally built AU). Not notarized — local/dev only. |

NOT yet done: `pluginval` (not installed) for the VST3; hands-on click-through of the effect
editor and parallel-branch latency; install-into-a-DAW test.

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
- Standalone app: `.../build/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.app`.

## Next steps (for whoever resumes)

1. `pluginval --strictness-level 8` against the built `.vst3` (install pluginval first).
2. Hands-on: add a simple AU (e.g. a gain/EQ) in the standalone, open its editor, verify audio
   passes and preset round-trips; then test in a DAW.
3. Validate parallel-branch latency alignment when an effect changes its own latency.
