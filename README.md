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

[Download for Windows](https://github.com/MeowXiaoXiang/xenia-canary-kbm/releases)
· [User guide](docs/kbm_input.md)
· [Tune your aim](docs/KBM/tuning.md)
· [Design and testing](docs/KBM/design-and-testing.md)

## Quick start

1. Launch the Windows build and open **KBM Controller Settings**.
2. In **Mouse**, enter your mouse's actual DPI. Start with **Fixed window** at
   **16 ms**. Set your keys and mouse buttons in **Button bindings**.
3. Choose **Save all and close**. In game, press **F8** to capture the mouse.

KBM Controller is the default Windows input backend. Use `--hid=any` for
general hardware controllers or `--hid=keyboard` for keyboard passthrough.

## What this fork changes

- Bind keyboard keys, mouse buttons and key combinations to Xbox 360 controls.
- Aim with Raw Input. Set the mouse speed needed to reach full stick and try it
  in a live preview before saving.
- Choose Fixed window, EMA smoothing or Direct. See the
  [tuning guide](docs/KBM/tuning.md#choose-a-processing-mode) for when to use each.
- View in-game diagnostics or record input for replay. The settings window
  supports English and Traditional Chinese.

## Building

Use the Windows prerequisites in the included upstream [building guide](docs/building.md),
then run `xb.bat setup` and `xb.bat build --config=release` in a Windows
developer environment. `xb.ps1` provides the same commands for PowerShell.
The executable is `build\bin\Windows\Release\xenia_canary.exe`.

This fork publishes unofficial Windows archives after `main` passes CI. See
[build provenance](docs/KBM/design-and-testing.md#build-provenance-and-upstream-updates)
for revision and release tags. Linux CI tests the portable KBM code. The KBM
backend and its settings window are available on Windows.

### Adding a host UI translation

Host UI translations are compile-time catalogs under
`src/xenia/app/localization/`. To add a language, register its canonical
BCP-47 code and native name, copy the complete English catalog, and keep every
`StringId` and format placeholder aligned. Languages using scripts not already
covered by the host font setup must also provide the corresponding Windows font
and glyph support.

## Scope and privacy

Do not add games, disc images, title content, saves, shader caches, logs, or
other proprietary Xbox assets to this repository. The project
does not contain game-specific hooks, memory patches, or title profiles.
For general emulator information and compatibility reports, see the
[upstream Xenia Canary project](https://github.com/xenia-canary/xenia-canary).
