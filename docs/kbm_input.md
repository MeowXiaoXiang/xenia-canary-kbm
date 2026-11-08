# KBM Controller keyboard and Raw Input mouse input

## Scope

KBM Controller is a Windows-only, experimental input backend. It maps keyboard keys
and mouse buttons to an emulated Xbox 360 controller, and translates Windows
Raw Input mouse movement into the emulated right stick. It is deliberately a
generic controller-input feature: it contains no game-specific memory hooks,
patches, or per-title profiles.
Windows is this fork's supported build and runtime target. Linux does not
build or expose a KBM Controller backend, selector, or settings UI. It can
build and test the portable KBM core in continuous integration.

## Build provenance and upstream updates

Every Windows build records two revisions. **Fork build commit** is the exact
commit from this repository that was compiled. **Upstream base commit** is the
merge base with `upstream/canary_experimental` — the Xenia Canary revision that
is actually present in that fork commit. Both are shown in the title and
startup log, and the Help menu opens each commit in its correct repository.
Published Windows archives use `kbm-<12-character fork commit>` tags; the tag
and the fork build commit refer to the same source revision.

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
   then choose **Save all**.

When KBM Controller is selected for the first time, the backend creates
`kbm.toml` in Xenia's storage root. This file intentionally remains
separate from `xenia-canary.config.toml`. It does not import or migrate a
previous `winkey.toml`; save the desired bindings again in the KBM Controller
Settings dialog.

New configurations use `schema_version = 4`. Only the current schema and
complete valid settings are accepted. Older versions (including v3), unknown
versions and malformed files are rejected as a whole, including custom bindings.
The old file is left untouched at startup and on Cancel.

For an incompatible file, choose **Restore all defaults...** in the settings window to
enable the save actions. This replaces the entire draft, including bindings,
with current defaults; edits still affect preview only until saved. **Save all** then replaces the old file with v4. There is no configuration
migration or automatic backup. A failed write preserves the old file and active
settings so the draft can be retried. Existing backup files are not deleted.

## Button bindings

The settings window keeps its save controls visible while content scrolls.
Edits, including **Restore all defaults...**, change a local draft and preview only.
**Save all** first publishes the draft to `kbm.toml`, then activates it.
**Save all and close** also returns to gameplay. Closing without saving
discards changes since the last successful save. The title-bar close button,
Escape (outside text editing, binding capture or preview), and reopening the
host menu entry ask whether to save, discard, or continue when the draft has
changes. **Cancel and close** explicitly discards the draft. Restoring defaults
requires confirmation and resets the entire draft, not just the visible section.
A failure preserves the active settings, saved file and draft so the player can retry.

The settings window starts at its narrow 520-unit width with three main
sections: **Button bindings**, **Mouse**, and **Advanced and diagnostics**,
in that order. Mouse starts expanded and keeps tuning, startup capture, the capture hotkey,
and an expandable live preview together. Button bindings and advanced tools
start collapsed. Widening the window places the complete bindings section on
the left and the complete mouse section on the right; advanced tools remain
below both columns. Within the running process, section state is retained
across reopening, resizing and host-language changes.

The 25 bindings use one compact list: input name, content-sized binding chips,
and a per-row actions menu. Alternative chips wrap when necessary. There are
no repeated group table headers or full-width binding-value bars.
Recording/writing opens the advanced section when it appears. Collapsing Mouse
or its preview stops the test and releases the cursor; collapsing Mouse also
cancels a pending capture-hotkey binding. Collapsing Button bindings cancels
its pending capture.
All sections share one draft and the fixed **Save all** controls. Speed and
minimum-output sliders share a row with their precise numeric inputs; DPI is
entered numerically. The window inherits Xenia's shared colors, padding and
control styling. The compact footer groups draft status with **Restore all
defaults...** and keeps the save actions below. The settings filename is listed
under **How these settings work**.

Press Escape to cancel binding capture. Click a binding chip to replace that
alternative while preserving the others. The per-row **...** menu adds an
alternative, assigns Escape, or clears the row. Multiple alternative keys for a
stick direction count as one direction even when held together. Bindings use stable logical tokens in
the saved file, not Windows virtual-key numbers: for example `Key.W`, `Key.F8`,
`Key.ArrowUp`, `Key.LeftShift`, `Mouse.Left`, and `Mouse.X1`. Chords use the
canonical order `Ctrl+Alt+Shift+Super+Key.F8`; a modifier by itself uses its
key token such as `Key.LeftShift`. The UI continues to display friendly names
like `F8` and `Mouse Left`. `Key.K` and `Ctrl+Key.K` may both activate while
Ctrl+K is held. The `_` and `^` case prefixes remain available. Legacy bare
keys, `VK_*`, and `0xNN` tokens are rejected by the current format.

Configuration saves write a temporary file before replacing `kbm.toml`, so a
failed write does not truncate the previously saved configuration.

Controller state and keystrokes use the same evaluated bindings. Modifier
chords work in either press order, and captured Raw Input mouse button
transitions are retained even between polls. Settings loaded from disk and
settings applied from the UI share range validation; non-finite physical tuning
values revert to defaults. A configuration with a wrong KBM value
type or invalid binding is rejected as a whole, so no partial settings are
applied.

KBM bindings are stored under `[HID.KBM.Bindings]` in `kbm.toml`; their keys
are concise names such as `a`, `left_trigger`, and `right_thumb_up` rather than
the runtime `kbm_keybind_*` CVar names. The upstream `keybind_*` settings in
the main configuration belong to the keyboard driver and do not control KBM.
Earlier experimental KBM builds shared those names incorrectly; re-save your
KBM bindings after updating.

If input is inactive, check that the main configuration selects `hid = "kbm"`.
An explicit `hid = "any"` selects general hardware backends and does not enable
KBM. Use `hid = "keyboard"` for upstream keyboard passthrough. The old
`winkey` selector is no longer supported.
Close host dialogs and focus the emulator before pressing F8. The log records
capture hotkey events, host UI suspension, and Raw Input capture transitions.

KBM Controller maps host input to the selected virtual Xbox 360 controller
slot. Click a binding field and press the desired key, mouse button, or
modifier chord. Use the row's **...** menu to add an alternative or clear it.
Bindings are displayed as names such as `F8` or `Ctrl+Shift+K`, rather than
raw Windows virtual-key codes.

KBM Controller does not expose keyboard passthrough. To expose a physical
keyboard to the guest, start Xenia with `--hid=keyboard` and configure the
upstream keyboard driver separately. `--hid=kbm` is the default Windows
virtual controller with Raw Input mouse-to-right-stick support; use
`--hid=any` for general hardware backend selection.

## Raw Input mouse tuning

Raw Input counts accumulated between guest controller polls are divided by
the actual elapsed time. There is no EMA, response exponent, displacement debt,
or guest-tick synchronization. The linear radial mapping preserves direction:

```text
full_scale_cps = dpi * full_stick_speed_cm_s / 2.54
magnitude = clamp(hypot(dx, dy) / elapsed_seconds / full_scale_cps, 0, 1)
```

The current starting settings are:

| Setting | Default | Range |
|---|---:|---:|
| Hardware DPI | 800; replace with the mouse's actual DPI | 100–32,000 |
| Full-stick speed | 10 cm/s | 1–100 cm/s |
| Stick anti-deadzone | On | On / Off |
| Minimum stick output | 0.30 | 0–0.5; used only with anti-deadzone |

These are product starting values, not detected hardware settings or a universal
calibration for every game. Existing valid v4 choices for anti-deadzone remain intact;
the default applies to new settings and **Restore all defaults...**.

- **Mouse hardware DPI** is manually entered, not automatically detected.
  Enter the actual hardware value and keep it fixed while tuning. This field
  changes the mapping scale; it does not change the mouse's hardware resolution.
- **Full-stick speed** is physical mouse movement speed, not cm/360 degrees.
  Lower values reach full stick more easily;
  higher values require faster movement. At the defaults, full stick starts near
  3149.6 counts/s. Defaults do not reproduce legacy tuning.
- **Stick anti-deadzone** is on by default. When enabled, nonzero radial
  output becomes `minimum + (1-minimum)*magnitude`. Minimum stick output defaults to
  0.30 with a 0–0.5 range (displayed as 30% and 0–50% in the UI). Zero motion always produces zero output; there is no
  smoothing tail. Compensation changes the otherwise linear output relationship.
- Finite out-of-range values are clamped; NaN and infinity revert to defaults.
  Reset/inactive samples and intervals over 100ms output zero. Nonpositive
  intervals leave pending counts for the next advancing poll and output zero.

### Tuning order and fine movement

1. Set the mouse's hardware DPI, then enter that same value in KBM settings.
2. Use the independent preview to adjust full-stick speed. Lower it for easier
   full deflection, or raise it for more physical speed before saturation.
3. Check the result in the game, whose own controller sensitivity, deadzone,
   acceleration and maximum turn speed still apply. Change one setting at a time.
4. Keep the minimum output at its starting value unless the game ignores small
   movements or minimum movement is too strong. Use **Save all** to activate
   the draft; preview changes alone do not change gameplay settings.

At low speeds, integer counts and different device/guest polling rates can
produce alternating zero and nonzero samples. A DPI mismatch can magnify each
count's output change. Anti-deadzone also introduces a minimum for each nonzero
sample; whether the camera moves smoothly then depends on the game's deadzone.
Entering the correct DPI does not itself add more hardware counts. A low-speed
boost curve would change gain, not add resolution, and can amplify small steps.

Use report replay to check the stick output sent by KBM. Reports do not record
camera angle or angular velocity, so they cannot alone prove the cause of an
in-game acceleration or a perceived change after a fast mouse movement.

Keyboard stick bindings combine with mouse output using per-axis saturation.
Removing smoothing can expose low-speed quantization and uneven device/poll
timing; it does not establish better gameplay frame times.

### Independent live preview

Choose **Start test** to capture the cursor while host game input remains
suspended. Move the mouse to see 0–100% output and the peak. Press Escape, Enter,
Space or **Stop test** to release it. Focus loss and closing the dialog also
stop the test; returning focus does not restart it. Stop retains the peak;
**Reset peak**, a new test or a mapping edit clears it.

Preview owns separate counts and a clock, sampled by the UI after at least
16ms using actual elapsed time. It uses the gameplay mapping math without
reading/clearing gameplay buckets or depending on guest polls. Consequently its
instantaneous output may differ from a gameplay sample. It also works before a
game is loaded and when the active configuration disables Raw Mouse. The F8
gameplay capture request remains unchanged.

## Input sampling diagnostics

The **Diagnostics** tool can record a 60-second gameplay sample. **Record
60-second diagnostics** automatically closes settings so guest input resumes.
When the draft has changes, **Save all and record for 60 seconds** saves them
first; a save failure leaves the window open and does not start recording.
Play normally, then reopen settings to see recording, writing or result status.
Technical details are collapsed separately and show the last gameplay sample,
not live preview output. Both displays measure radial stick magnitude, clamped
to 100%. The CSV is stored beside
`kbm.toml` as `kbm-input-report-<timestamp>.csv`.

Each report includes the Raw Input delta, unfiltered velocity, mouse right-stick
output, game input polling interval, capture state, and a summary of reset,
stale, or dropped samples. New reports identify `mapper=physical_linear_radial`
and `estimator=poll_bucket_actual_time` in their metadata. Reports are published
after recording ends, so recording does not add per-sample disk I/O.

Schema 2 and 3 reports also write `kbm-input-report-<timestamp>.events.csv`, containing
accepted Raw Input events, reset markers, and the initial bucket state.
The timestamps describe host processing, not hardware sampling. Event sequence
numbers associate poll buckets with the event trace. The poll report is published
last and marks a completed pair; a sidecar alone is not a complete report.
Activating changed KBM settings finishes the current capture so its metadata stays valid.

Skip all leading `#` metadata comments when importing, rather than a fixed
number of lines. Schema 3 uses `mapper=physical_linear_radial` and
`estimator=poll_bucket_actual_time`, with DPI and cm/s metadata and
`velocity_x`/`velocity_y` columns. Schema 2 retains its legacy EMA interpretation.
Both use actual elapsed intervals; older reports used
a 1 ms floor. Dropped counters describe recorder capacity, not OS packet loss.
The event buffer supports up to 600,000 entries and the poll buffer 120,000;
reports expose truncation counts. Keep both files when sharing a new capture.

For read-only poll replay, run `python tools/kbm_replay.py <report.csv>`.
Its output compares timing calculations, not measured camera response. Reports
without event sidecars cannot reconstruct individual Raw Input arrivals, and
host-processing timestamps do not establish hardware sampling times.

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

## Input lifecycle regression checks

Use a separate test directory and disposable configuration files. Record the
fork revision, integrated upstream base, mouse settings, and results before
changing input behavior. A successful build or portable-core test run does not
verify native Windows event ordering or a game's camera response.

| Check | Procedure | Expected result |
| --- | --- | --- |
| Focus loss | Hold a movement key or mouse button, switch to another application, release it there, and return. | No stuck control. Requested mouse capture can resume on return. |
| Capture toggle | Move the mouse while repeatedly capturing and releasing with F8. | Cursor visibility and clipping match capture state; old movement does not reappear after capture resumes. |
| Host dialogs | Open and close settings while moving or holding a control; repeat with another host dialog. | Guest input is suspended, the cursor is available, and closing the last dialog restores requested capture without stale input. |
| Binding capture | Capture a key, mouse button, and modifier chord; cancel capture with Escape. | Correct binding names, working cancellation, and no unintended controller input while the dialog is open. |
| Save and cancel | Edit a setting, save it, reopen, then edit and cancel. | Draft edits affect preview only; cancellation retains the last successfully activated settings. |
| Live preview | Start a test before loading a game; move, stop, reset the peak, and change tuning. | Output updates without guest polls; stopping returns to zero, and reset or tuning changes clear the peak. |
| Preview capture | Test Escape, Enter, Space, focus loss, binding capture and dialog closure. | Cursor is released; returning focus does not restart preview, and the gameplay F8 request is preserved. |
| Incompatible configuration | Open a v3 or unknown-version file; cancel, reopen, restore defaults and save. | No old fields load; save actions require confirmed Restore all defaults. Cancel preserves the file; successful Save replaces it with v4 without a backup. |
| XAM UI | Exercise game-requested XAM dialogs with short and long durations, with and without a controller connected. | No crash or false successful keystroke; no old mouse movement after the dialog closes. |
| Diagnostic completion | Record a full 60-second gameplay sample while recording frame times. | Reports are readable; compare frame times around completion with a run without sampling. Track any completion stall separately from mouse mapping behavior. |

Host dialogs suspend KBM through the ImGui dialog lifecycle. The XAM UI-active
flag and the host dialog becoming visible are separate transitions; check their
ordering rather than assuming they change simultaneously. The mouse processor
also resets capture history and rejects poll intervals greater than 100 ms.
This timeout describes time between controller polls, not the age of each Raw
Input event, and does not replace focus and capture reset checks.

Host-dialog suspension clears logical keyboard state on both entry and resume.
Keys received for UI navigation or binding capture must not become held guest
controls when the last dialog closes. A new press can activate the binding
again after resume.

Diagnostic completion transfers the completed buffers to one report worker.
The controller query does not copy the buffers, serialize CSV, or write files.
The worker publishes the event file first and the poll CSV last as the report
pair's completion marker. New reports use schema 3; replay still recognizes
legacy report math and rejects unknown schemas/models.

The settings page shows a saving message until publication finishes. Another
session can start after saving completes; a write failure is shown instead of a
successful report path. Stop requests publication asynchronously, while Cancel
discards an active recording. A report already being saved is allowed to finish.
Shutdown waits for an accepted report to finish and discards an unfinished
recording. The Windows application explicitly shuts down sampling before its
quick-exit path, which bypasses ordinary driver destruction. Frame-time
comparisons during gameplay remain useful because moving
work off the input thread does not eliminate all CPU and storage contention.

Sampling buffers and the report worker belong to the sampling component. Buffer
allocation and lazy worker startup happen on the UI path before input locks are
acquired. The driver snapshots initial mouse state and activates recording under
its existing input locks. Producers then take only a short session mutex to
append or transfer ownership; the worker releases that mutex before report work
and never accesses the driver, UI, or emulator global critical region.
Manual Stop briefly takes the input region to keep the recording cutoff ordered
with an in-flight poll; report processing still occurs entirely on the worker.

## Build

To try the Windows backend, download the unofficial archive from this fork's
[Releases](https://github.com/MeowXiaoXiang/xenia-canary-kbm/releases). The
archive is published only after the `main` build and portable KBM tests pass.
You can also build the same source locally. Use the Windows section of
[the included upstream build guide](building.md).
On a Windows development environment, the normal verification commands are:

```powershell
.\xb.bat setup
.\xb.bat build --config=release
```

`xb.ps1` provides the same commands for PowerShell.

The locally built Release executable is
`build\bin\Windows\Release\xenia_canary.exe`.

Windows Ninja builds detect the compiler's localized `/showIncludes` prefix
without converting UTF-8 diagnostics through the system ANSI code page. This
allows header changes to trigger incremental recompilation on localized MSVC
installations. For a build directory created before this detection was added,
run `xb.bat build --config=release --force` once to replace objects whose header
dependencies may be missing. Changing Windows language settings or installing
an English compiler language pack is not required.

To run the Windows KBM core, configuration, and localization regression tests
from a developer environment:

```powershell
.\xb.bat build --config=release --build-tests `
  --target=xenia-hid-kbm-core-tests `
  --target=xenia-hid-kbm-config-tests `
  --target=xenia-hid-kbm-lifecycle-tests `
  --target=xenia-hid-kbm-sampling-tests `
  --target=xenia-app-localization-tests
ctest --test-dir build -C Release --output-on-failure `
  -R "^xenia-(hid-kbm-(core|config|lifecycle|sampling)-tests|app-localization-tests)$"
```

These tests cover portable state and mapping behavior, configuration rejection
and failed replacement, catalog consistency, and driver focus and host-dialog
transitions using an in-process test window. The driver tests do not register
Raw Input, create a native window, or validate cursor clipping and capture.
Sampling tests also cover worker handoff, capacity limits, cancellation,
shutdown, writer failures, CSV compatibility and publication cleanup. Windows
CI runs these five suites before preparing an artifact. Run the native Windows
lifecycle checks above in addition to these automated tests.

The dependency-prefix parser has separate Python regression tests:

```powershell
python -B tools/build/msvc_showincludes_test.py
```

From an MSVC developer environment with CMake and Ninja on PATH, set
`$env:XENIA_TEST_MSVC_DEPS = "1"` before running this command to also check real
C and C++ header-triggered rebuilds in a disposable temporary directory.

Linux does not produce a KBM runtime executable. To validate the portable KBM
core and host UI localization catalog from WSL, configure a separate build
directory with tests enabled:

```bash
cmake -S . -B build-linux-kbm -G Ninja -DXENIA_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS=-mmovdir64b
cmake --build build-linux-kbm \
  --target xenia-hid-kbm-core-tests xenia-hid-kbm-sampling-tests xenia-app-localization-tests
ctest --test-dir build-linux-kbm -R "^xenia-(hid-kbm-(core|sampling)-tests|app-localization-tests)$" \
  --output-on-failure
```

The extra `-mmovdir64b` flag works around an upstream GCC 15 configuration
issue in Xenia's unrelated memory implementation; it is not a KBM runtime
requirement.
