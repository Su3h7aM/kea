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
- **Text insertion:** the Wayland `input-method-unstable-v1` protocol (what KWin
  implements; see `InputMethodV1Interface`). One input method per seat.
- **Target:** KDE Plasma 6 / Wayland + PipeWire. X11 and PulseAudio-only systems are
  out of scope for v1.

The authoritative design is [`docs/rfc-0001-kea.md`](docs/rfc-0001-kea.md). Read it
before making architectural changes.

## Status

**Pre-alpha (Phases 0–4 implemented).** Module layout:

```
src/app/          # tray, settings, readiness, model downloader, Main.qml
src/audio/        # resampler, wav loader, QAudioSource recorder
src/controller/   # DictationController + ParakeetWorker (QThread)
src/hotkey/       # KGlobalAccel push-to-talk
src/inference/    # dlopen parakeet backend (offline + streaming)
src/insert/       # input-method-v1 + TextCommitter
cmake/parakeet.cmake  # ExternalProject fetch of parakeet.cpp (CPU + Vulkan)
data/protocols/   # wayland XML
data/models.json  # default model catalog
```

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

## Build

Toolchain: CMake + extra-cmake-modules, Qt 6 (Core, Gui, Qml, Quick, QuickControls2,
Multimedia, WaylandClient), KF 6 (Kirigami, I18n, CoreAddons, Config, GlobalAccel,
StatusNotifierItem, IconThemes). parakeet.cpp is **not** a submodule — it is fetched and
built at `cmake --build` time by `cmake/parakeet.cmake` (via `ExternalProject_Add`) into
two self-contained `libparakeet.so` variants (CPU and Vulkan), which Kea loads at runtime
via `dlopen`.

### GUI only (fast, no ASR build)

```
cmake -B build -DKEA_BUILD_PARAKEET=OFF
cmake --build build
./build/bin/kea                    # run (or QT_QPA_PLATFORM=offscreen ./build/bin/kea)
./build/bin/kea-resampler-test     # DSP unit tests
./build/bin/kea-committer-test     # TextCommitter unit tests (mock context)
./build/bin/kea-controller-test    # DictationController state smoke (missing model → Error)
```

### Runtime flow (Phase 3)

1. `GlobalHotkey` (default Meta+Shift+V) via `KGlobalAccel::globalShortcutActiveChanged` for hold-to-talk.
2. `DictationController` starts mic (`AudioRecorder`) and opens a parakeet stream on a **dedicated worker thread**.
3. PCM blocks are queued to `ParakeetWorker::feedPcm`; finalized text is committed via `TextCommitter`.
4. Hotkey release / tray Stop finalizes the stream and commits the tail.
5. Settings (`QSettings` under `kea/kea`): model path, backend (CPU/Vulkan), hotkey.
   Model path default: `$KEA_MODEL` if set, else `~/.local/share/kea/models/tdt-0.6b-v3-q8_0.gguf`.
   Offline models buffer until hotkey release; streaming models feed live.

### With the parakeet backends (fetches + builds parakeet.cpp + ggml, slow on first run)

```
cmake -B build -DKEA_BUILD_PARAKEET=ON
cmake --build build --target parakeet_all   # builds CPU (+Vulkan if available)
cmake --build build                          # builds kea + kea-parakeet-smoke
```

The smoke test loads a backend and transcribes a WAV:

```
./build/bin/kea-parakeet-smoke <cpu|vulkan> <model.gguf> <audio.wav>
```

### Conventions

- KDE builds with `QT_NO_KEYWORDS`: use `Q_SIGNALS`/`Q_SLOTS`/`Q_EMIT`, never bare
  `signals:`/`slots:`/`emit`.
- The Kirigami pattern is `add_executable(kea)` **before** `ecm_add_qml_module(kea ...)`,
  otherwise the target becomes a shared plugin with no `main()` entry point.
- The dlopen loader (`src/inference/parakeet_backend.*`) declares its own opaque
  `parakeet_ctx` and does **not** include the upstream `parakeet_capi.h`, so it compiles
  before the build-time fetch has run.

## Key invariants (do not regress)

- **parakeet.cpp's C-API is not thread-safe per context.** All `stream_*` calls for one
  session must run on the same worker thread. Never share a context across threads.
- **One input method per Wayland seat.** Kea occupies that slot; it cannot coexist with
  an already-running IME (fcitx5/IBus). Detect and warn at startup.
- **KWin uses `input_method_unstable_v1`, not v2.** All insertion code targets v1
  (`commit_string(serial, text)` / `preedit_string`; serial comes from `commit_state`).
- **All commits go through `TextCommitter`.** Never call the Wayland context directly
  from the controller. Serial bookkeeping and skip-when-inactive live only there.
- **Two `.so` variants share an identical C-API.** CPU and Vulkan libs are selected at
  runtime via `dlopen`. CPU must remain a working fallback when Vulkan is unavailable.

## Parakeet streaming contract (the core of the pipeline)

Kea feeds **16 kHz mono float32** PCM to `parakeet_capi_stream_feed`, which returns only
the text **newly finalized** since the last call and an EOU/EOB event bitmask. PipeWire
delivers 48 kHz int16 audio, so Kea resamples 48 kHz → 16 kHz and int16 → f32 before
feeding. On hotkey-up, `parakeet_capi_stream_finalize` flushes the tail.

See `include/parakeet_capi.h` in the upstream parakeet.cpp repo (fetched at build time into
`build/parakeet/<variant>-src/`) for the authoritative API.
