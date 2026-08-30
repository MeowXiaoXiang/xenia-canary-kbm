# Changelog

All notable user-facing changes to Xenia Canary - WinKey Input are documented
here. This project follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html) for its
source milestones.

## [Unreleased]

Future entries should describe user-visible WinKey changes, maintenance changes
that affect building or configuration, and upstream integrations that change
the compatibility baseline.

## [0.1.0] - 2026-08-30

First public source milestone. Tagged from upstream-integrated merge commit
`7acdad2`; the embedded Xenia Canary base is `0c843efb3`.

### Added

- Optional Windows WinKey backend for keyboard and Raw Input mouse control of
  a virtual Xbox 360 controller.
- Standalone `winkey.toml` storage for bindings and Raw Input mouse settings.
- Click-to-bind keyboard keys, mouse buttons, alternatives, and modifier
  chords displayed as readable names.
- Mouse capture controls, sensitivity, aim-curve, full-stick speed, and
  optional minimum stick-output compensation.
- English and Traditional Chinese host UI, including a runtime interface
  language selector.
- Fork and upstream-base build provenance in the UI, logs, exception reports,
  and Help menu.
- Public WinKey documentation and repository maintenance guidance.

### Notes

- This is a Windows-only, experimental, source-only project. Build locally
  using the documented Xenia Canary prerequisites.
- It contains no title-specific memory hooks, patches, profiles, or
  proprietary Xbox assets.
