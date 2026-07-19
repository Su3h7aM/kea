# Kea

> On-device voice dictation for Linux and KDE Plasma (Wayland).

Kea is a system-wide dictation app in the spirit of [Wispr Flow](https://wisprflow.ai/),
but local-first, open source, and native to the Linux desktop. Place your cursor in
any text field, hold a global push-to-talk hotkey, speak, and transcribed text is
committed into the focused field — entirely on your own machine.

## Status

**Pre-alpha (Phases 0–4 scaffolded).** The end-to-end path works: hotkey → mic →
parakeet (streaming or offline) → Wayland text insertion. You need a GGUF model and a
free input-method seat on Plasma Wayland. Design details live in
[RFC 0001](docs/rfc-0001-kea.md).

## Stack

| Concern | Technology |
| --- | --- |
| UI | **Kirigami** + QML on **Qt 6** |
| Speech recognition | **[parakeet.cpp](https://github.com/mudler/parakeet.cpp)** (streaming + offline) |
| LLM post-process | **[llama.cpp](https://github.com/ggml-org/llama.cpp)** + LFM2.5-230M GGUF |
| Inference backends | **CPU** and **Vulkan** for ASR (runtime `dlopen`) |
| Audio capture | **PipeWire** hosts via Qt 6 Multimedia |
| Text insertion | **Wayland `input-method-unstable-v1`** (KWin) |
| Global hotkey | `KGlobalAccel` (default **Ctrl+Shift+D**) |
| Tray | `KStatusNotifierItem` |

## Quick start

### Build

parakeet.cpp (ASR) and llama.cpp (optional polish LLM) are **always** built — first
configure is slow while both are fetched.

```sh
cmake -B build
cmake --build build -j"$(nproc)"
./build/bin/kea
```

### First run

1. Open Kea from the tray (or the window that appears on first launch).
2. **Download the default ASR model** (offline TDT) or pick a catalog entry.
3. Optionally enable **Post-process** and download **LFM2.5 230M** for Correct/Enhance styles.
4. On Plasma **Wayland**, ensure no other IME (fcitx5/IBus) owns the seat.
5. Click **Start** to load the model, focus a text field, hold **Ctrl+Shift+D**,
   speak, release to commit.

### Tests

```sh
./build/bin/kea-resampler-test
./build/bin/kea-committer-test
./build/bin/kea-controller-test
```

## Design

See [`docs/rfc-0001-kea.md`](docs/rfc-0001-kea.md) for architecture, risks, and the
phased plan. Contributor notes are in [`AGENTS.md`](AGENTS.md).

## License

MIT. See [`LICENSE`](LICENSE). Model weights are under their upstream licenses
(NVIDIA Parakeet / OpenMDW as applicable).
