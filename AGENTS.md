# Xenia Canary - KBM Controller Input

This is a public, Windows-focused fork of Xenia Canary. It adds the optional
KBM Controller backend for keyboard bindings and Raw Input mouse-to-controller
input.
It is a research and source repository; it does not publish prebuilt releases.

## Upstream and branch policy

- Keep `upstream` pointed at `https://github.com/xenia-canary/xenia-canary.git`.
- Track the upstream `canary_experimental` branch and keep fork-specific work
  in clearly scoped commits on the published feature branch.
- Never hardcode revision identifiers in source. `xenia-build.py` records the
  fork `HEAD` and the merge base with `upstream/canary_experimental` so a build
  shows both the fork commit and the upstream revision actually integrated.
- After an upstream update: fetch it, integrate the intended revision, verify
  `git merge-base HEAD upstream/canary_experimental`, rebuild, and check that
  the displayed upstream base matches that result.

## Scope

- Keep KBM changes Windows-specific, opt-in, and generic. Do not add
  game-specific memory hooks, game patches, or title profiles.
- Keep `--hid=keyboard` as the upstream keyboard passthrough backend and
  `--hid=kbm` as the opt-in virtual controller backend.
- Do not add game ISOs, title content, saves, shader caches, logs, captures,
  or any proprietary Xbox files to Git.
- Keep user-facing strings in `src/xenia/app/localization.cc` synchronized
  between English and Traditional Chinese. When adding Chinese text, update
  the explicit glyph list in `src/xenia/ui/imgui_drawer.cc` as well.
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

The expected release executable is:

```text
build\bin\Windows\Release\xenia_canary.exe
```

Never overwrite a user's normal emulator installation during development;
test from a separate directory.

## Commit discipline

- Before committing, run `git diff --check` and inspect the staged diff.
- Use scoped Conventional Commit subjects and an explanatory body for
  non-trivial changes.
- Run the formatter/linter and a Release build in proportion to the changed
  C++ or build-generator scope before presenting work as ready.
