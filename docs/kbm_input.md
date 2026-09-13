# KBM Controller keyboard and Raw Input mouse input

## Scope

KBM Controller is a Windows-only, experimental input backend. It maps keyboard keys
and mouse buttons to an emulated Xbox 360 controller, and translates Windows
Raw Input mouse movement into the emulated right stick. It is deliberately a
generic controller-input feature: it contains no game-specific memory hooks,
patches, or per-title profiles.

## Build provenance and upstream updates

Every local build records two revisions. **Fork build commit** is the exact
commit from this repository that was compiled. **Upstream base commit** is the
merge base with `upstream/canary_experimental` — the Xenia Canary revision that
is actually present in that fork commit. Both are shown in the title and
startup log, and the Help menu opens each commit in its correct repository.

Do not edit either revision by hand. `xenia-build.py` calculates the upstream
base automatically whenever it generates `build/version.h`. When updating
upstream, fetch `upstream`, integrate the intended
`upstream/canary_experimental` revision, then rebuild and confirm that the
displayed upstream base has advanced as expected. This keeps the public fork
traceable even when its own input commits are newer than the integrated Canary
revision.

## Enable and configure

1. On Windows, KBM Controller is selected by default. You can also select it
   explicitly with `--hid=kbm`.
2. Open **KBM Controller Settings** from the host menu. The menu appears when KBM Controller is
   selected, or after a `kbm.toml` file already exists.
3. Enable **KBM Controller**, select its controller slot, configure bindings,
   then choose **Save**.

When KBM Controller is selected for the first time, the backend creates
`kbm.toml` in Xenia's storage root. This file intentionally remains
separate from `xenia-canary.config.toml`. It does not import or migrate a
previous `winkey.toml`; save the desired bindings again in the KBM Controller
Settings dialog.

## Keyboard bindings

The settings window keeps its save controls visible while the content scrolls.
Use **Save and close** to return to gameplay, or **Save** to keep editing.
Unsaved edits are discarded on close. The diagnostics show the last gameplay
sample, not live input while the settings window pauses the controller.

Press Escape to cancel binding capture. Use the **Esc** button next to a binding
to assign Escape itself. Multiple alternative keys for a stick direction count
as one direction even when held together. Bindings allow additional modifiers:
`K` and `Ctrl+K` may both activate while Ctrl+K is held.

Configuration saves write a temporary file before replacing `kbm.toml`, so a
failed write does not truncate the previously saved configuration.

Controller state and keystrokes use the same evaluated bindings. Modifier
chords work in either press order, and captured Raw Input mouse button
transitions are retained even between polls. Settings loaded from disk and
settings applied from the UI share range validation; non-finite sensitivity
and curve values revert to defaults.

KBM bindings are stored as `kbm_keybind_*` under `[HID.KBM]` in
`kbm.toml`. The upstream `keybind_*` settings in the main configuration belong
to the keyboard driver and do not control KBM. Earlier experimental KBM builds
shared those names incorrectly; re-save your KBM bindings after updating.

If input is inactive, check that the main configuration selects `hid = "kbm"`.
An explicit `hid = "any"` selects general hardware backends and does not enable
KBM. Use `hid = "keyboard"` for upstream keyboard passthrough. The old
`winkey` selector is no longer supported.
Close host dialogs and focus the emulator before pressing F8. The log records
capture hotkey events, host UI suspension, and Raw Input capture transitions.

KBM Controller maps host input to the selected virtual Xbox 360 controller
slot. Click a binding field and press the desired key, mouse button, or
modifier chord. Use **+** to add an alternative binding and **x** to clear one.
Bindings are displayed as names such as `F8` or `Ctrl+Shift+K`, rather than
raw Windows virtual-key codes.

KBM Controller does not expose keyboard passthrough. To expose a physical
keyboard to the guest, start Xenia with `--hid=keyboard` and configure the
upstream keyboard driver separately. `--hid=kbm` is the default Windows
virtual controller with Raw Input mouse-to-right-stick support; use
`--hid=any` for general hardware backend selection.

## Raw Input mouse tuning

Raw Input deltas are sampled as counts per second, multiplied by
**Sensitivity**, shaped by **Aim curve**, and converted to the right-stick
range. The game still applies its own controller sensitivity, turn-speed, and
deadzone behavior.

- **Sensitivity** is a multiplier. The UI reset value, **10x**, is an editable
  initial reference for a 3600 DPI mouse, not a universal recommendation.
- **Base full-stick speed** is the counts-per-second threshold at which the
  emulated stick reaches full deflection. The default is **24000 counts/s**.
  Lowering it reaches the game's maximum turn speed sooner; raising it leaves
  more room for fine movement.
- **Aim curve** of 1.0 is linear. Values above 1.0 slow very small movements;
  values below 1.0 boost them.
- **Minimum stick output** is optional. Enable it only if the game's analog
  deadzone swallows small Raw Input output; it intentionally changes the
  near-center feel.

Start from the defaults, adjust the game's own controller sensitivity, and
then make small changes to the KBM Controller controls. DPI alone cannot predict the
final turning distance because games map an analog stick differently.

## Mouse capture

The default capture hotkey is **F8**, configurable as a key or modifier chord.
When capture is active, KBM Controller hides and clips the cursor, requests exclusive
Raw Input, and sends a status notification. Press the hotkey again to release
it. Losing focus releases capture; returning focus can restore a requested
capture. Opening host UI temporarily suspends KBM Controller input so its controls
remain usable.

Overlays and focus changes are controlled by Windows and may interrupt capture.
If capture no longer behaves as expected, release it with the configured
hotkey, focus the Xenia window, and enable it again.

## Limits

- This is an experimental Windows input path, not native mouse support inside
  an Xbox 360 title.
- The emulated right stick has a finite range and games may impose turn-speed
  limits, acceleration, or deadzones.
- The backend is intended to coexist with upstream Xenia Canary behavior; do
  not rely on it for competitive or anti-cheat-protected online play.

## Build

Follow [the upstream build guide](building.md). On a Windows development
environment, the normal verification commands are:

```powershell
.\xb.bat setup
.\xb.bat build --config=release
```

The release executable is
`build\bin\Windows\Release\xenia_canary.exe`.
