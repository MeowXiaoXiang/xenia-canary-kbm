# KBM Mouse Tuning

[User guide](../kbm_input.md) · [How it works and how to test it](design-and-testing.md)

## Set a baseline

1. New settings start at 800 DPI. Check the active DPI in your mouse software
   and enter it in **Mouse**. KBM uses that value to estimate how fast you move
   the mouse.
2. Start with **Fixed window**, **16 ms**. Leave **Full-stick speed** at
   **10 cm/s** and the minimum output at **30%** for the first test.
3. Choose **Start test**. Try a small correction, then a fast swipe. If you
   reach 100% too easily, raise the full-stick speed. If a comfortable fast
   swipe stays below 100%, lower it.
4. Choose **Save all and close** and try the same movements in game. Change one
   setting at a time so you can tell what made the difference.

Full-stick speed is how fast the mouse must move to reach 100% stick output.
At 5 cm/s, half the mouse speed reaches full stick compared with 10 cm/s.
The game controls how fast the camera turns at that output. In-game stick
sensitivity, deadzones, acceleration and turn limits can differ by title.

## Choose a processing mode

| Mode | Starting value | Range | What to expect |
| --- | ---: | --- | --- |
| **Fixed window** | **16 ms** | 4–24 ms | A longer window keeps more recent movement for steadier slow aim. A shorter window clears old motion sooner and can miss sparse events. |
| **EMA smoothing** | **8 ms** | 2–32 ms | Increase the time constant for stronger smoothing. The output can take longer to settle after you stop. |
| **Direct** | Uses the game query interval | — | Responds to motion since the previous query. Slow speed can vary more between queries. |

The Fixed window panel shows the latest interval, median, P95 and P99 of
game polls in the last five seconds, up to 1,024 samples. It updates while
the game keeps polling, including while settings are open. Intervals longer
than 100 ms are excluded. Coverage is the share of those intervals at or
below the window length. Movement can still produce stick output when the
interval is longer than the window.

Use **Show in-game diagnostics** in Fixed window or EMA mode. The diagnostics
overlay previews the selected mode, timing and appearance while KBM settings
are open. If the draft mode or its time setting differs from the active one,
the preview asks you to save before it shows live data. With matching settings
and no game running, it displays **Waiting for a game to start**.

- **Fixed window** shows the active window length, coverage, latest game
  query interval, median, P95 and P99. Values refresh every half second.
- **EMA** plots up to five seconds of raw and smoothed speed, capped at
  4,096 samples. The chart refreshes up to 30 times per second, using the
  configured DPI and a shared cm/s scale. Each valid EMA update during mouse
  capture supplies one sample, including updates with zero movement. The
  readouts show the latest speeds and the new sample's weight.
  The dashed guides and their colored numbers mark the two latest speeds.
  The shared vertical axis starts at 0 and has a minimum upper limit of
  2 cm/s. Higher speeds in the last five seconds raise that limit.
  When you stop moving and game queries continue, the speeds return to zero
  while earlier motion scrolls out of the chart.
  Use **Diagnostics overlay transparency** in the EMA settings to adjust the
  background.
- **Direct** hides the overlay. Switching back to Fixed window or EMA
  restores the saved overlay preferences.

The overlay defaults to off, in the lower-right corner, with 54% EMA
background transparency. Changes preview immediately in KBM settings. Save
to keep them across launches, or cancel to restore the saved values. The
three preferences are stored under `[HID.KBM.Diagnostics]` in `kbm.toml`.
Enabling the overlay can move presentation to the UI thread on
configurations that otherwise present directly from the guest output thread.

The two time controls use 0.5 ms steps and remember their values separately.
New or reset settings use the values in the table. Explicit values saved in
`kbm.toml` carry over. Missing v4 fields use the current defaults. The mode
and time settings are saved under
`[HID.KBM.RawMouse.Tuning]`.

## Tune by feel

| What you notice | First change to try | What to look for |
| --- | --- | --- |
| Slow aim moves in steps | Check the active DPI in your mouse software against the DPI in KBM. If they match, compare Fixed 16 ms with a longer window. | See whether the crosshair tracks more evenly. Longer windows can leave more motion after you stop or reverse. |
| Small corrections reach full stick too easily | Raise **Full-stick speed**. | More room for small corrections before saturation. Fast turns will need a faster swipe to reach 100%. |
| A fast swipe feels too slow | Watch the preview while making a comfortable fast swipe. Lower **Full-stick speed** if the stick stays below 100%. | If the stick already reaches 100%, check the game's own sensitivity and turn limit. |
| The camera keeps moving briefly after you stop | Compare Fixed 16 ms with 12 ms, or shorten the EMA time constant. | Check whether stopping improves without making slow aim uneven. |
| The smallest step feels too strong, or small movements do nothing | Adjust **Minimum stick output** with anti-deadzone enabled. Lower it for a gentler first step, or raise it if small stick commands fall inside the game's deadzone. | Higher values can help clear the deadzone and can make the first visible step larger. A zero mouse speed estimate still sends zero. |

Hardware DPI ranges from 100 to 32,000. Full-stick speed ranges from 1 to
100 cm/s. Stick anti-deadzone starts on with a minimum output of 30%, adjustable
from 0% to 50%. It raises the minimum for nonzero output and can help small
movements get past the game's deadzone. An accurate DPI value gives a better
speed estimate. It does not increase the number of counts the mouse reports.

### Choose an anti-deadzone shape

Use the **Circle** and **Axial** buttons beside **Stick anti-deadzone**.
Circle is the default. It keeps one minimum output for the whole vector and
preserves the estimated movement direction. Axial provides separate
**Horizontal minimum output** and **Vertical minimum output** controls,
each starting at 30% with a 0–50% range. All three minimum values are saved
independently, so switching shapes retains their settings.

Axial raises each nonzero component independently. It can help when a game
applies its deadzone separately to horizontal and vertical input. Tiny motion
across an axis can produce a direction jump. Compare slow horizontal,
vertical, diagonal and near-horizontal movements before choosing it.
Both shapes send zero for a zero speed estimate. Turning compensation off
uses linear radial mapping.

Each output axis is limited to 100%. With Axial enabled, the combined
diagonal magnitude can exceed 100%. The live preview, diagnostics and CSV
show that magnitude. A full progress bar represents 100%; its number can be
higher with Axial. The full-stick speed sets where the underlying radial
speed estimate saturates. Compensation can bring the combined magnitude to 100%
earlier. All three processing modes and the independent preview use the same
compensation rules.

The v4 settings under `[HID.KBM.RawMouse.Tuning]` are `anti_deadzone_shape`
(`radial` or `axial`), `minimum_response`, `minimum_response_x`, and
`minimum_response_y`. Older v4 files keep their radial behavior. Missing
axial minima default to 30%. Save to apply changes to gameplay.

## Compare two settings

Use a familiar scene and keep the game's sensitivity, mouse DPI, full-stick
speed and minimum output the same while comparing time settings. Follow a
straight edge slowly, reverse direction sharply, then aim at a point and stop.
Switch back to the first value once to check that the difference repeats.
Use [input sampling](../kbm_input.md#input-sampling-diagnostics) and the
[replay tools](design-and-testing.md#replay-tools) if you want to compare the
actual stick commands sent by KBM.
