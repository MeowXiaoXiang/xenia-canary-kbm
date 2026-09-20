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
keyboard passthrough. It is not an official Xenia Canary build or support channel.
This repository does not publish releases; build it locally when you want to
try the experimental input path. General emulator information, compatibility
reports, and upstream contribution guidance belong to the
[Xenia Canary project](https://github.com/xenia-canary/xenia-canary).

## What this fork changes

- Separate `kbm.toml` storage, grouped into controller, bindings, Raw Mouse,
  and Raw Mouse tuning sections.
- Click-to-bind keyboard, mouse-button, alternative, and modifier-chord
  mappings for a virtual Xbox 360 controller.
- Raw Input mouse-to-right-stick translation with configurable sensitivity,
  radial response curve, time-based smoothing, full-stick threshold, capture,
  and optional minimum-output compensation.
- Versioned `kbm.toml` storage using logical input tokens such as `Key.W` and
  `Mouse.Left`, intentionally rejecting incompatible KBM settings rather than
  silently mixing old mapping behavior with new input.
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

Use the Windows prerequisites in the included upstream
[building guide](docs/building.md), then run `xb.bat setup` and
`xb.bat build --config=release` from a Windows developer environment.
`xb.ps1` provides the same commands for PowerShell. The expected executable is
`build\\bin\\Windows\\Release\\xenia_canary.exe`.

## Scope and privacy

Do not add games, disc images, title content, saves, shader caches, logs, or
other proprietary Xbox assets to this repository. The project deliberately
does not contain game-specific hooks, memory patches, or title profiles.
