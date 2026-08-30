<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="256px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/256.png" />
    </a>
</p>

<h1 align="center">Xenia Canary — WinKey Input</h1>

This is a Windows-focused experimental fork of
[Xenia Canary](https://github.com/xenia-canary/xenia-canary). It adds an
opt-in WinKey backend that maps keyboard and Windows Raw Input mouse movement
to an emulated Xbox 360 controller.

It is not an official Xenia Canary build or support channel. This repository
does not publish releases; build it locally when you want to try the
experimental input path. General emulator information, compatibility reports,
and upstream contribution guidance belong to the
[Xenia Canary project](https://github.com/xenia-canary/xenia-canary).

## What this fork changes

- Separate `winkey.toml` storage for keyboard bindings and Raw Input mouse
  settings.
- Click-to-bind keyboard, mouse-button, alternative, and modifier-chord
  mappings for a virtual Xbox 360 controller.
- Raw Input mouse-to-right-stick translation with configurable sensitivity,
  response curve, full-stick threshold, capture, and optional minimum-output
  compensation.
- English and Traditional Chinese host UI. Technical terms remain English where
  that is clearer.

See [WinKey Input](docs/winkey_input.md) for setup, tuning, capture behavior,
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
