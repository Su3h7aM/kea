# Kea

> On-device, streaming voice dictation for Linux and KDE Plasma (Wayland).

Kea is a system-wide dictation app in the spirit of [Wispr Flow](https://wisprflow.ai/),
but local-first, open source, and native to the Linux desktop. Place your cursor in
any text field, press a global push-to-talk hotkey, speak, and your words stream into
the focused field — transcribed entirely on your own machine, with no cloud dependency
and no audio ever leaving the process.

## Status

**Early / pre-alpha.** Design is captured in [RFC 0001](docs/rfc-0001-kea.md);
implementation has not started. APIs, file layout, and behavior are all subject to
change.

## Stack

| Concern | Technology |
| --- | --- |
| UI | **Kirigami** + QML on **Qt 6** |
| Speech recognition | **[parakeet.cpp](https://github.com/mudler/parakeet.cpp)** (ggml inference of NVIDIA Parakeet ASR), streaming with end-of-utterance detection |
| Inference backends | **CPU** and **Vulkan** GPU (user-selectable) |
| Audio capture | **PipeWire** via Qt 6 Multimedia |
| Text insertion | **Wayland `input-method-v2`** protocol |
| Global hotkey | `KGlobalAccel` |
| Tray | `KStatusNotifierItem` |

## Target platform

KDE Plasma 6 on **Wayland** with the **PipeWire** audio stack. v1 targets new systems
only — X11 and PulseAudio-only setups are out of scope.

## Goals for v1

- Global push-to-talk hotkey that works from any focused text field.
- Low-latency **streaming** transcription (words appear as you speak).
- On-device inference only — no network, no account, no audio upload.
- Two selectable backends: CPU and Vulkan.
- System-tray icon + a Kirigami settings window.

An LLM-based cleanup/formatting layer (Wispr Flow's signature feature) is intentionally
deferred to a later phase.

## Design

See [`docs/rfc-0001-kea.md`](docs/rfc-0001-kea.md) for the full RFC: architecture,
data flow, the text-insertion design, risks, and a phased implementation plan.

## License

MIT. See [`LICENSE`](LICENSE).
