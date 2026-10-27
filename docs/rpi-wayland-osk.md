# Raspberry Pi OS (Trixie) — Wayland on-screen keyboard

TONE3000's standalone build runs natively on Wayland (JUCE's `wayland`
preview branch) and drives Raspberry Pi OS's on-screen keyboard
(**squeekboard**) through the standard Wayland text-input protocol. This page
covers building, launching, and verifying that on a Raspberry Pi 5 kiosk.

## How it works

- The standalone app uses JUCE's Wayland backend when `WAYLAND_DISPLAY` is
  set (plugin windows stay on X11/XWayland by JUCE design — no OSK there).
- Kiosk builds fill `WAYLAND_DISPLAY` in from `$XDG_RUNTIME_DIR/wayland-N`
  when it is unset, before JUCE opens a display
  (`plugin/ui/core/LinuxBackend.cpp`). The editor's `Constructing:` log line
  records `backend wayland|X11 (WAYLAND_DISPLAY=…)`; if the guard cannot
  secure Wayland the app logs a warning and shows a "No on-screen keyboard"
  toast.
- Focusing any text field enables `zwp_text_input_v3`; labwc forwards the
  request to squeekboard (input-method-v2), which shows the keyboard. Tapping
  keys commits text back through the same surface.
- squeekboard is an **overlay** — it covers the bottom ~205 px of the 1024×600
  panel instead of resizing the window. The UI keeps a focused text field
  scrolled above it via the `T3K_OSK_HEIGHT` inset (see below).
- The kiosk build (`-DT3K_TOUCH_UI=ON`) lays the UI out in a 1024×600 design
  box (desktop is 1024×578), so at 1x it fills the panel with no letterbox;
  `T3K_OSK_HEIGHT` is in design px either way.

## Prerequisites (already present on RPiOS Trixie)

- Compositor `labwc` (0.20.x) advertising `zwp_text_input_manager_v3`,
  `zwp_input_method_manager_v2`, `zwp_virtual_keyboard_manager_v1`.
- `squeekboard`, enabled from **Control Centre → On-screen Keyboard**. That
  writes `/etc/xdg/autostart/squeekboard.desktop` (which runs `/usr/bin/sbtest`
  and starts squeekboard when a touch device is present) and sets
  `SQUEEKBOARD_PREFERRED_OUTPUT`. It must be running **before** a field asks
  for input — a fresh login starts it automatically.

## Build

```sh
cd /path/to/tone3000-plugin
git submodule update --init --recursive
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/linux-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DT3K_TOUCH_UI=ON \
  -DBUILD_AAX=OFF -DBUILD_LV2=OFF -DBUILD_CLAP=OFF
cmake --build build --target TONE3000_Standalone -j4
# binary: build/plugin/TONE3000_artefacts/RelWithDebInfo/Standalone/TONE3000
```

## Install from the release tarball

The Linux release tarball ships `install.sh` plus the kiosk supervisor
(`tone3000-autostart`). `--kiosk` is opt-in:

```sh
tar -xzf TONE3000-v*-linux-aarch64.tar.gz
cd TONE3000-v*-linux-aarch64
./install.sh --kiosk
```

Besides the plugin formats and the standalone app, this:

- installs `~/.local/bin/tone3000-autostart`;
- adds a marked block to `~/.config/labwc/autostart` that runs it (creating
  the file if needed; an existing line is left alone, so re-running is safe);
- merges `<windowRules>` for `identifier="TONE3000"` (undecorated +
  maximized) and a touchscreen fallback into `~/.config/labwc/rc.xml`.
  If you have no `rc.xml`, it is seeded from `/etc/xdg/labwc/rc.xml` so the
  RPiOS theme and keybinds survive.

The launcher waits for the MOTU M2 card (`T3K_AUDIO_DEVICE` overrides the
name; set it empty to skip the wait), exports `T3K_OSK_HEIGHT=220`, and
restarts the app only after a crash. Logs live in
`~/.local/state/tone3000/` (`autostart.log`, `app.out`).

A touchscreen the distro config does not know about can be pinned while
installing:

```sh
T3K_TOUCH_DEVICE="wch.cn USB2IIC_CTP_CONTROL (USB 3-2)" \
T3K_TOUCH_OUTPUT=HDMI-A-1 ./install.sh --kiosk
```

Log out and back in after installing so labwc runs the new autostart.
`./install.sh --uninstall` removes the launcher and the marked kiosk blocks.

## Run (from an SSH shell, or a session launcher)

The installed `tone3000-autostart` supervisor is the supported way to launch
the kiosk (see above); this manual invocation is for debugging.

```sh
export XDG_RUNTIME_DIR=/run/user/1000
export WAYLAND_DISPLAY=wayland-0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export T3K_OSK_HEIGHT=220        # keep focused fields above the keyboard

build/plugin/TONE3000_artefacts/RelWithDebInfo/Standalone/TONE3000
```

`T3K_OSK_HEIGHT` is the keyboard's on-screen height in **design px** (editor
scale 1). `0` (default) leaves desktop behaviour unchanged; the Pi kiosk uses
`220` (squeekboard's 4-row layout measures ~205 at 1024 wide). Only the
focused `TextEditor` is affected — `DragScroller` scrolls it above the
keyboard on focus; other controls keep their normal focus margin.

## Verify

```sh
# OSK is running and (once a field is focused) visible
pgrep -x squeekboard
busctl --user get-property sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 Visible

# Text-input protocol traffic (run the app with WAYLAND_DEBUG=1)
grep -E 'zwp_text_input_v3#.*(enable|disable|set_cursor_rectangle|commit_string)' /tmp/app.log

# Screenshot of the live session
grim /tmp/osk.png
```

Expected: `enable` + `set_cursor_rectangle` on focus (caret y above the
keyboard's top edge, ~395 on the 1024×600 panel), `commit_string` per key,
`disable` on blur, and `Visible` flipping `false ↔ true`.

## Troubleshooting

- **No keyboard appears.** Confirm squeekboard is running before the app
  (`pgrep -x squeekboard`) and `WAYLAND_DISPLAY` is set in the app's
  environment. `WAYLAND_DEBUG=1` on the app shows whether
  `zwp_text_input_manager_v3` is bound and `enable` is sent.
- **Field hidden behind the keyboard.** Raise `T3K_OSK_HEIGHT` (the value is
  the occluded band); the field scrolls up further on focus.
- **Falls back to X11.** `WAYLAND_DISPLAY` must name a live socket *and* the
  app must be the standalone target (plugin formats are X11-only by design).
  Kiosk builds auto-select the display and log `backend X11` plus a toast when
  that failed; look for the `[Startup] WAYLAND_DISPLAY=…` line on stderr.

