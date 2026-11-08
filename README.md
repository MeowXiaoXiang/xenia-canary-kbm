<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="256px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/256.png" />
    </a>
</p>

<h1 align="center">Xenia Canary — KBM Controller Input</h1>

This is a Windows-only experimental fork of
[Xenia Canary](https://github.com/xenia-canary/xenia-canary). It adds a KBM
Controller backend that maps keyboard and Windows Raw Input mouse movement to
an emulated Xbox 360 controller.

KBM Controller is built and available only on Windows. Linux continuous
integration builds and tests its portable mapping, state, mouse, and reporting
core only; it exposes no KBM HID backend, selector, or settings UI.

On Windows, KBM Controller is the default HID backend. Use `--hid=any` to
restore general hardware backend selection, or `--hid=keyboard` for explicit
keyboard passthrough. It is not an official Xenia Canary build or support
channel. This fork publishes unofficial
[Windows releases](https://github.com/MeowXiaoXiang/xenia-canary-kbm/releases)
after `main` passes CI. Each `kbm-<12-character fork commit>` tag identifies the
source used for its archive; the application also shows the fork commit and
integrated upstream base. General emulator information, compatibility reports,
and upstream contribution guidance belong to the
[Xenia Canary project](https://github.com/xenia-canary/xenia-canary).

## What this fork changes

- Versioned `kbm.toml` storage, grouped into controller, bindings, Raw Mouse,
  and Raw Mouse tuning sections, with logical tokens such as `Key.W` and
  `Mouse.Left`. Incompatible files require an explicit reset; they are not
  migrated or backed up automatically.
- Click-to-bind keyboard, mouse-button, alternative, and modifier-chord
  mappings for a virtual Xbox 360 controller.
- Raw Input mouse-to-right-stick translation with manual DPI and physical
  full-stick speed, linear radial mapping, independent live preview, and capture.
  Configurable stick anti-deadzone defaults on with 0.30 minimum output.
- A single-page KBM settings window with mouse preview, compact bindings,
  diagnostic recording, and shared Save all controls.
- English and Traditional Chinese host UI. Technical terms remain English where
  that is clearer.

See [KBM Controller Input](docs/kbm_input.md) for setup, tuning, capture behavior,
and known limits.

### Adding a host UI translation

Host UI translations are compile-time catalogs under
`src/xenia/app/localization/`. To add a language, register its canonical
BCP-47 code and native name, copy the complete English catalog, and keep every
`StringId` and format placeholder aligned. Languages using scripts not already
covered by the host font setup must also provide the corresponding Windows font
and glyph support.

## Building

Download the Windows archive from this fork's
[Releases](https://github.com/MeowXiaoXiang/xenia-canary-kbm/releases) to try a
verified build. For a local build, use the Windows prerequisites in the included
upstream [building guide](docs/building.md), then run `xb.bat setup`
and `xb.bat build --config=release` from a Windows developer environment.
`xb.ps1` provides the same commands for PowerShell. The expected executable is
`build\bin\Windows\Release\xenia_canary.exe`.

## Scope and privacy

Do not add games, disc images, title content, saves, shader caches, logs, or
other proprietary Xbox assets to this repository. The project deliberately
does not contain game-specific hooks, memory patches, or title profiles.
