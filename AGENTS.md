# Xenia Canary - KBM Controller Input

This is a public, Windows-focused fork of Xenia Canary. It adds the optional
KBM Controller backend for keyboard bindings and Raw Input mouse-to-controller
input.
It publishes unofficial Windows builds from verified `main` commits. It does
not publish games, title content, or other proprietary Xbox files.

## Upstream and branch policy

- Keep `upstream` pointed at `https://github.com/xenia-canary/xenia-canary.git`.
- Track the upstream `canary_experimental` branch and keep fork-specific work
  in clearly scoped commits on `main`.
- Never hardcode revision identifiers in source. `xenia-build.py` records the
  fork `HEAD` and the merge base with `upstream/canary_experimental` so a build
  shows both the fork commit and the upstream revision actually integrated.
- After an upstream update: fetch it, integrate the intended revision, verify
  `git merge-base HEAD upstream/canary_experimental`, rebuild, and check that
  the displayed upstream base matches that result.
- Automatic releases use `kbm-<12-character fork commit>` tags. Publish only
  after lint, the Windows build, and portable Linux KBM tests pass on a `main`
  push; pull requests and manual CI runs must not publish releases.

## Scope

- Keep KBM changes Windows-specific and generic. Do not add
  game-specific memory hooks, game patches, or title profiles.
- Keep `--hid=keyboard` as the explicit upstream keyboard passthrough backend.
  On Windows, `--hid=kbm` is the default virtual controller backend and
  `--hid=any` remains available for general hardware backend selection.
- Do not add game ISOs, title content, saves, shader caches, logs, captures,
  or any proprietary Xbox files to Git.
- Keep user-facing strings in the English and Traditional Chinese catalogs under
  `src/xenia/app/localization/` synchronized. The Traditional Chinese font glyph
  ranges are derived from its catalog in `src/xenia/ui/imgui_drawer.cc`.
- Keep public documentation focused on `README.md` and `docs/kbm_input.md`.
  Do not expose private installation paths, personal test data, or unreleased
  local experiments.

## Verification

From a Windows developer environment with the Visual C++ tools, Windows SDK,
CMake, Ninja, and Vulkan SDK available, run:

```powershell
.\xb.bat setup
.\xb.bat build --config=release
python xenia-build.py lint --all
```

The expected locally built Release executable is:

```text
build\bin\Windows\Release\xenia_canary.exe
```

Never overwrite a user's normal emulator installation during development;
test from a separate directory.

## Commit discipline

- Before committing, run `git diff --check` and inspect the staged diff.
- Use upstream-style scoped subjects such as `[HID/KBM] Description` and an
  explanatory body for non-trivial changes.
- Run the formatter/linter and a Release build in proportion to the changed
  C++ or build-generator scope before presenting work as ready.
