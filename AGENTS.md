# AGENTS.md

Durable reference for humans and AI coding agents working on Kea.

## What this project is

Kea is a system-wide voice dictation app for KDE Plasma on Wayland — a local-first,
open-source alternative to Wispr Flow. Press a global push-to-talk hotkey, speak, and
transcribed text streams into the focused text field. Everything runs on-device.

- **UI:** Kirigami + QML on Qt 6.
- **ASR engine:** [parakeet.cpp](https://github.com/mudler/parakeet.cpp) — the C++/ggml
  inference port of NVIDIA Parakeet, used in its **streaming** mode with end-of-utterance
  (EOU) detection.
- **Inference backends:** CPU and Vulkan (two `libparakeet` variants, runtime-selected).
- **Audio:** PipeWire, captured via Qt 6 Multimedia (`QAudioSource`).
- **Text insertion:** the Wayland `input-method-v2` protocol (the only permission-free
  way to commit text into arbitrary surfaces on Wayland).
- **Target:** KDE Plasma 6 / Wayland + PipeWire. X11 and PulseAudio-only systems are
  out of scope for v1.

The authoritative design is [`docs/rfc-0001-kea.md`](docs/rfc-0001-kea.md). Read it
before making architectural changes.

## Status

**Pre-alpha, design only.** No source has been written yet. The RFC defines the planned
module layout (`src/audio`, `src/inference`, `src/insert`, `src/controller`, `src/app`,
etc.); expect the tree to grow into that shape during Phase 0.

## Version control: Jujutsu (jj), colocated

This is a **colocated** jj repo (both `.jj/` and `.git/` present). Use `jj` for all
mutations. A few things that trip up git users:

- There is no staging area and no `git add`. Edits are auto-snapshotted into the working
  copy (`@`). Describe a change with `jj describe -m "..."`, then start the next change
  with `jj new`.
- Bookmarks (branches) do **not** auto-advance when you commit. Move/push them
  explicitly (`jj bookmark move`, `jj git push`).
- Never run bare interactive `jj` commands (`jj describe`/`jj commit`/`jj split`/
  `jj squash -i`/`jj resolve` without arguments) — they open an editor and will hang.
  Always pass `-m`, a fileset, or use the non-interactive flag.
- Conflicts never block an operation; they ride along inside the resulting commit.
  `jj undo` / `jj op log` is the safety net when something looks wrong.
- Prefer read-only `git` (`git log`, `git show`) over raw mutating `git` commands; if you
  must use one, `jj undo` can revert it.

The default remote bookmark is `main` on GitHub.

## Commits

Write commit titles that describe what changed in the code. Do not put issue numbers,
ticket IDs, or "Closes #N" in the title — link issues in the body when useful.

## Build (once source exists)

Planned toolchain: CMake + extra-cmake-modules, Qt 6 (Core, Qml, Quick, QuickControls2,
Multimedia, WaylandClient), KF 6 (Kirigami, I18n, CoreAddons, Config, GlobalAccel,
StatusNotifierItem, IconThemes). parakeet.cpp is vendored as a submodule under
`3rdparty/parakeet.cpp` and built as two shared libraries (CPU and Vulkan) with an
identical flat C-API surface.

```
cmake -B build
cmake --build build
```

## Key invariants (do not regress)

- **parakeet.cpp's C-API is not thread-safe per context.** All `stream_*` calls for one
  session must run on the same worker thread. Never share a context across threads.
- **One input method per Wayland seat.** Kea occupies that slot; it cannot coexist with
  an already-running IME (fcitx5/IBus). Detect and warn at startup.
- **Wayland `commit()` requires the latest `done()` serial.** All input-method commits
  are centralized; a mismatched serial silently drops text. Never commit without a
  matching `done`.
- **Two `.so` variants share an identical C-API.** CPU and Vulkan libs are selected at
  runtime via `dlopen`. CPU must remain a working fallback when Vulkan is unavailable.

## Parakeet streaming contract (the core of the pipeline)

Kea feeds **16 kHz mono float32** PCM to `parakeet_capi_stream_feed`, which returns only
the text **newly finalized** since the last call and an EOU/EOB event bitmask. PipeWire
delivers 48 kHz int16 audio, so Kea resamples 48 kHz → 16 kHz and int16 → f32 before
feeding. On hotkey-up, `parakeet_capi_stream_finalize` flushes the tail.

See `include/parakeet_capi.h` in `3rdparty/parakeet.cpp` for the authoritative API.
