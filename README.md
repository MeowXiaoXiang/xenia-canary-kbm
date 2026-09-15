<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="256px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/256.png" />
    </a>
</p>

<h1 align="center">Xenia Canary — KBM Controller Input</h1>

This is a Windows-focused experimental fork of
[Xenia Canary](https://github.com/xenia-canary/xenia-canary). It adds a KBM
Controller backend that maps keyboard and Windows Raw Input mouse movement to
an emulated Xbox 360 controller.

Linux source builds remain supported for build compatibility, but the KBM
Controller backend itself is not available outside Windows.

On Windows, KBM Controller is the default HID backend. Use `--hid=any` to
restore general hardware backend selection, or `--hid=keyboard` for explicit
keyboard passthrough. It is not an official Xenia Canary build or support channel.
This repository
does not publish releases; build it locally when you want to try the
experimental input path. General emulator information, compatibility reports,
and upstream contribution guidance belong to the
[Xenia Canary project](https://github.com/xenia-canary/xenia-canary).

## What this fork changes

- Separate `kbm.toml` storage for keyboard bindings and Raw Input mouse
  settings.
- Click-to-bind keyboard, mouse-button, alternative, and modifier-chord
  mappings for a virtual Xbox 360 controller.
- Raw Input mouse-to-right-stick translation with configurable sensitivity,
  response curve, time-based smoothing, full-stick threshold, capture, and
  optional minimum-output compensation.
- English and Traditional Chinese host UI. Technical terms remain English where
  that is clearer.

See [KBM Controller Input](docs/kbm_input.md) for setup, tuning, capture behavior,
and known limits.

## Building

Use the upstream [building guide](docs/building.md) and run the standard
`xb.bat setup` then `xb.bat build --config=release` commands from a Windows
developer environment. The expected executable is
`build\\bin\\Windows\\Release\\xenia_canary.exe`.

## Scope and privacy

Do not add games, disc images, title content, saves, shader caches, logs, or
other proprietary Xbox assets to this repository. The project deliberately
does not contain game-specific hooks, memory patches, or title profiles.
