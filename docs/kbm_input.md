# KBM Controller Guide

On Windows, KBM Controller maps keyboard and mouse input to an emulated Xbox
360 controller.

[Quick start](#enable-and-configure) · [Tune your aim](KBM/tuning.md)
· [How KBM works and how to test it](KBM/design-and-testing.md)

## Enable and configure

1. Download the [Windows build](https://github.com/MeowXiaoXiang/xenia-canary-kbm/releases)
   and open **KBM Controller Settings** from the menu.
2. Enable **KBM Controller** and select a controller slot. Bind your keys and
   mouse buttons under **Button bindings**.
3. In **Mouse**, enable **Use mouse to control the right stick** and enter your
   mouse's actual DPI. Start with **Fixed window** at **16 ms**.
4. Choose **Save all and close**. In game, press **F8** to capture the mouse.

If the menu is missing or KBM input is inactive, launch with `--hid=kbm` or
set `hid = "kbm"` in `xenia-canary.config.toml`. KBM is the default Windows
backend. Use `hid = "any"` for general hardware controllers or
`hid = "keyboard"` for upstream keyboard passthrough. The menu can still
appear when a `kbm.toml` file exists and another backend is active.

### Settings files and reset

When first selected, the KBM backend creates `kbm.toml` beside Xenia's main
configuration in the storage root. Enter bindings from older `winkey.toml`
files again.

If Xenia rejects an older or invalid `kbm.toml`, choose **Restore all defaults...**
before saving. This resets all settings in the window, including bindings.
**Save all** replaces the old file with schema v4. Copy the old file first if
you want a backup. Startup, cancellation and a failed save leave the old file
in place.

## Settings window

- **Save all** writes all changes and applies them. **Save all and close** also
  closes the window. Use live preview to test changes before saving.
- **Cancel and close** discards edits since the last successful save. With
  unsaved edits, closing from the title bar, pressing Escape outside an edit
  or test, or choosing the menu entry again asks what to do.
- The three sections are **Button bindings**, **Mouse**, and **Advanced and
  diagnostics**. In a wide window, bindings and mouse settings appear side by
  side. The save controls stay visible at the bottom.

### Button bindings

Click a binding to replace it, or use the **...** menu to add an alternative,
assign Escape, or clear the row. Press Escape during capture to cancel it.
Multiple bindings for the same stick direction count as one direction even
when held together.

In `kbm.toml`, bindings use tokens such as `Key.W`, `Mouse.Left` and
`Ctrl+Shift+Key.F8`. See the [binding format](KBM/design-and-testing.md#binding-format)
for the full rules.

## Raw Input mouse tuning

Start with Fixed 16 ms and your mouse's actual DPI. Use the
[mouse tuning guide](KBM/tuning.md) to adjust slow aim, full-stick speed and
anti-deadzone, or to compare Fixed window, EMA and Direct.

The anti-deadzone buttons offer **Circle** with one minimum, or **Axial**
with separate horizontal and vertical minima. See
[anti-deadzone shapes](KBM/tuning.md#choose-an-anti-deadzone-shape) for tuning.

### Independent live preview

Choose **Start test** in **Mouse**. Move slowly and then swipe quickly to see
the estimated speed, right-stick output and peak. You can test before loading
a game. The preview uses the settings in the window.

Press Escape, Enter, Space or **Stop test** to release the cursor. Losing focus,
closing the window or collapsing the preview also stops the test.
**Reset peak**, a new test or a change to the mapping clears the peak.

## Mouse capture

Press **F8** to capture or release the mouse. Choose a different keyboard key
or combination under **Mouse**, or clear the binding to disable the hotkey.
Enable **Capture mouse when KBM Controller starts** to request it on startup.

Capture hides and clips the cursor. Losing focus releases it, and returning
can restore capture if it was requested. Host dialogs pause gameplay input.
If capture feels stuck after leaving an overlay, release it, focus Xenia,
then capture again.

## In-game diagnostics

See [Show in-game diagnostics](KBM/tuning.md#choose-a-processing-mode) to
display the Fixed window statistics or the EMA chart while you play.

## Input sampling diagnostics

1. In **Advanced and diagnostics**, choose **Record 60-second diagnostics**.
   The settings window closes so you can play. If you have edits, choose
   **Save all and record for 60 seconds** to save them first.
2. Play normally, then reopen settings to see the status or report path.
   Recording ends after 60 seconds even if the game stops polling.
   **Stop and save report** saves the recording so far. **Cancel sampling**
   discards an active recording.
3. Keep the report and its matching `.events.csv` file together. Both are
   saved beside `kbm.toml` and include the game name and title ID when
   available at the start of recording.

The report measures controller input and output. See the
[replay tools](KBM/design-and-testing.md#replay-tools) to check a recording
and learn what it can tell you.

## Mouse timing experiments

See the [timing chart and experiments](KBM/design-and-testing.md#mouse-timing-experiments)
for the same synthetic movement at several window lengths, along with the
settings and commands to repeat the comparison.

## Limits

KBM sends emulated right-stick input. The game still applies its controller
deadzone, acceleration and turn-speed limit. This experimental backend is
intended for general controller input on Windows.

## Build

See [build and test commands](KBM/design-and-testing.md#build) and
[manual regression checks](KBM/design-and-testing.md#input-lifecycle-regression-checks)
for changes to KBM. Linux CI covers portable KBM code.

### Build provenance and upstream updates

See [build provenance and upstream updates](KBM/design-and-testing.md#build-provenance-and-upstream-updates)
for the revision identifiers and upstream verification.
