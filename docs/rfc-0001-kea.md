# RFC 0001: Kea — Offline Voice Dictation for KDE Plasma

| Field | Value |
| --- | --- |
| **Title** | Kea: On-device, streaming voice dictation for Linux / KDE Plasma (Wayland) |
| **Status** | Draft |
| **Date** | 2026-07-17 |
| **Stack** | C++17 · Qt6 / Kirigami · parakeet.cpp (ggml) · PipeWire · Wayland input-method-v2 |
| **Target platform** | KDE Plasma 6 on Wayland, PipeWire audio stack (new systems only) |
| **Inspiration** | Wispr Flow — but local-first, open, and native to the Linux desktop |

---

## 1. Abstract

**Kea** is a system-wide voice dictation application for KDE Plasma on Wayland. Place your cursor in any text field, press a global push-to-talk hotkey, speak, and your words stream into the focused field as finalized text — transcribed entirely on your own machine, with no cloud dependency and no audio ever leaving the process.

Speech recognition is powered by **parakeet.cpp**, the C++/ggml inference port of NVIDIA's Parakeet ASR models. The UI is a **Kirigami** + QML application with a system-tray presence and a settings window. Audio is captured via **PipeWire** (through Qt6 Multimedia). Transcribed text is injected into other applications using the native **Wayland `input-method-v2` protocol**, the same mechanism fcitx5 and Maliit use to feed text to the compositor.

v1 ships **pure streaming speech-to-text** with two inference backends — **CPU** and **Vulkan** GPU — user-selectable. An LLM-based cleanup/formatting layer (the Wispr Flow "polish" feature) is explicitly deferred to a later phase, but the architecture leaves a clean seam for it.

---

## 2. Background & Motivation

Wispr Flow proved the demand for an "anywhere, press-and-speak" dictation experience with live cleanup and formatting. It is, however, macOS/Windows/iOS/Android only and cloud-based. Linux users have no first-class equivalent: existing options (built-in Plasma dictation, whisper.cpp wrappers, browser-based tools) are either non-global, non-streaming, accuracy-limited, or require an internet connection.

parakeet.cpp closes the engine gap: it delivers NVIDIA Parakeet accuracy **faster than real-time on CPU** (RTFx well above 1) and faster still on GPU, byte-identical to NeMo, with a small footprint (the 110M streaming model is a few hundred MB). Combined with Kirigami for a native Plasma look-and-feel, this makes a polished, private, always-on dictation tool finally viable on the Linux desktop.

The hard problem is not recognition — it is **injecting text into arbitrary applications on Wayland**, where the old X11 tricks (`xdotool`, `XSendEvent`) do not work. Kea solves this by registering as a Wayland input method.

---

## 3. Goals & Non-Goals

### Goals (v1)

- **G1.** Global push-to-talk hotkey that starts/stops dictation from any focused text field.
- **G2.** Low-latency **streaming** transcription: words appear as you speak, not after you stop.
- **G3.** On-device inference only — no network calls, no audio upload, no account.
- **G4.** Two selectable backends: **CPU** and **Vulkan**, chosen in settings.
- **G5.** System-tray icon + a Kirigami settings window (hotkey, model, device, backend).
- **G6.** Runs on KDE Plasma 6 / Wayland with the PipeWire audio stack.
- **G7.** Clean seam for a future LLM post-processing layer without redesign.

### Non-Goals (v1)

- ❌ LLM cleanup / formatting / command mode / rewrite transforms (Phase 2+).
- ❌ Transcript history, scratchpad, personal dictionary, snippets.
- ❌ X11 support, PulseAudio-only systems, or non-Linux platforms.
- ❌ Multilingual auto-switching as a first-class UI (the engine supports it; v1 targets the English streaming model).
- ❌ Coexistence with an already-running IME (fcitx5/IBus) — see **§9 Risks**.

---

## 4. High-Level Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                            Kea process                                │
│                                                                       │
│   ┌─────────────┐    PCM 16k mono f32    ┌────────────────────────┐  │
│   │  Audio In   │ ─────────────────────▶ │   parakeet.cpp (ggml)  │  │
│   │ QAudioSource│   resample 48k→16k     │   streaming RNN-T +    │  │
│   │ (PipeWire)  │                         │   EOU detection        │  │
│   │ int16→f32   │                         │   backend: CPU|Vulkan  │  │
│   └─────────────┘                         └───────────┬────────────┘  │
│          ▲                                             │ finalized    │
│          │ start/stop                                   │ text + EOU   │
│          │                                              ▼              │
│   ┌──────┴───────┐   QML/signals   ┌──────────────────────────────┐  │
│   │  Hotkey      │◀───────────────▶│   Dictation Controller        │  │
│   │ KGlobalAccel │                 │   (state machine, threading)  │  │
│   └──────────────┘                 └───────────────┬──────────────┘  │
│                                                     │ commit_string   │
│   ┌──────────────┐                                  ▼                 │
│   │ System Tray  │                      ┌─────────────────────────┐   │
│   │ KStatusNotif │                      │ Text Insertion (input   │   │
│   │ ierItem      │                      │ method-v2 client)       │   │
│   └──────────────┘                      └────────────┬────────────┘   │
└──────────────────────────────────────────────────────┼────────────────┘
                                          wl_seat / zwp_input_method_v2
                                                        │
                                                        ▼
                                             ┌────────────────────┐
                                             │  KWin (compositor)  │
                                             │  → focused surface  │
                                             └────────────────────┘
```

**Data flow:** hotkey → controller starts capture + opens a parakeet streaming session → `QAudioSource` frames are resampled and fed to `parakeet_capi_stream_feed` → finalized text is committed via the input method into the focused field → on release, `stream_finalize` flushes the tail and the final text is committed.

---

## 5. Core Technology Decisions

| Concern | Choice | Rationale |
| --- | --- | --- |
| UI toolkit | **Kirigami + QML** over Qt6 | Native Plasma look/feel, convergent components, KDE-recommended. |
| ASR engine | **parakeet.cpp** (streaming) | Best Linux CPU RTFx; byte-identical NeMo parity; flat C-API + streaming with EOU; no Python at runtime. |
| Inference backends | **CPU** and **Vulkan** (two `libparakeet` variants, runtime-selected) | Covers headless/integrated-GPU users (CPU) and discrete-GPU users (Vulkan). |
| Audio capture | **PipeWire via Qt6 Multimedia `QAudioSource`** | Qt ≥ 6.10 has a native PipeWire backend; consistent API; no raw PipeWire C needed. |
| Global hotkey | **KGlobalAccel** | Plasma-native global shortcut registration, user-editable in System Settings. |
| Text insertion | **Wayland `input-method-v2`** (`zwp_input_method_v2`) | Only protocol-based, permission-free way to commit text to arbitrary surfaces on Wayland. |
| Tray presence | **KStatusNotifierItem** | Plasma-native status notifier; works on Wayland. |
| Process model | **Single foreground process** with tray + settings | No daemon split needed; keeps the audio↔ASR↔insertion path in one process for latency. |

### 5.1 Why parakeet.cpp streaming specifically

The `parakeet_realtime_eou_120m-v1` model is **cache-aware streaming** with **end-of-utterance (EOU)** detection. The flat C-API surfaces exactly what a dictation app needs:

```c
parakeet_ctx    *ctx = parakeet_capi_load("parakeet_realtime_eou_120m-q8_0.gguf");
parakeet_stream *s   = parakeet_capi_stream_begin(ctx);
int eou = 0;
// feed a block of 16 kHz mono f32 PCM; returns newly-finalized text since last call
char *text = parakeet_capi_stream_feed(s, pcm, n_samples, &eou);  // "" if none yet
// ...text is appended incrementally; eou mask marks a complete utterance...
char *tail = parakeet_capi_stream_finalize(s);  // flush end-of-stream tail
parakeet_capi_stream_free(s);
```

- `stream_feed` returns **only newly-finalized text** each call — ideal for committing words to the field as they lock in, and for showing the not-yet-finalized tail as a live preedit.
- The `eou` bitmask (`PARAKEET_EVENT_EOU` / `PARAKEET_EVENT_EOB`) lets Kea flush per-utterance boundaries naturally.
- It expects **16 kHz mono float32** PCM; Kea resamples PipeWire's 48 kHz int16 down to 16 kHz f32 before feeding.

### 5.2 Why input-method-v2 is the insertion mechanism (and its cost)

Wayland clients **cannot inject keyboard events** into surfaces they don't own — `xdotool`/`XSendEvent` are X11-only, and KWin does **not** support the `virtual-keyboard` protocol for arbitrary clients (so `wtype` fails on Plasma). The only permission-free, protocol-correct path is to register as the seat's **input method** and `commit_string` text into the focused `text-input` surface — exactly what fcitx5 and Maliit do.

**Cost / constraint:** the protocol mandates *"no more than one input method object per seat."* Kea occupies that slot. This is the single biggest design risk (§9).

---

## 6. Detailed Design

### 6.1 Repository & module layout

```
kea/
├── CMakeLists.txt                 # top-level: ECM, Qt6, KF6, vendored parakeet
├── org.kde.kea.desktop
├── 3rdparty/
│   └── parakeet.cpp/              # submodule; builds two .so variants
├── data/
│   ├── protocols/
│   │   └── input-method-unstable-v2.xml   # from wayland-protocols
│   └── models.json                # downloadable model catalog
└── src/
    ├── main.cpp                   # QQmlApplicationEngine + loadFromModule
    ├── CMakeLists.txt
    ├── app/                       # QML (Kirigami UI)
    │   ├── Main.qml               # tray-prefs window shell
    │   ├── pages/
    │   │   ├── SettingsPage.qml   # hotkey, model, device, backend
    │   │   └── ModelManagerPage.qml
    │   └── Indicators.qml         # floating "listening" overlay
    ├── audio/
    │   ├── audio_recorder.h/.cpp  # QAudioSource wrapper (QObject, QML_ELEMENT)
    │   └── resampler.h/.cpp       # 48k int16 → 16k mono f32
    ├── inference/
    │   ├── parakeet_backend.h/.cpp# dlopen loader + streaming wrapper
    │   └── device.h               # Backend enum {CPU, Vulkan}
    ├── hotkey/
    │   └── global_hotkey.h/.cpp   # KGlobalAccel action collection
    ├── tray/
    │   └── tray_icon.h/.cpp       # KStatusNotifierItem
    ├── insert/
    │   ├── input_method.h/.cpp    # QWaylandClientExtensionTemplate binding
    │   └── text_committer.h/.cpp  # commit_string + preedit orchestration
    └── controller/
        └── dictation_controller.h/.cpp  # the state machine, QML_SINGLETON
```

### 6.2 The dictation controller (state machine)

A `QML_SINGLETON` `DictationController` drives the whole pipeline. States:

```
        ┌─────────┐  hotkey-down       ┌───────────┐  stream_finalize  ┌────────┐
        │  IDLE   │──────────────────▶ │ LISTENING │─────────────────▶│ DRAIN  │
        └─────────┘                    └─────┬─────┘                  └───┬────┘
              ▲                             │ hotkey-up                   │
              │                             ▼                             │
              │                       ┌───────────┐                       │
              │                       │  (commit  │                       │
              │                       │ finalized │                       │
              │                       │  on EOU)  │                       │
              │                       └───────────┘                       │
              └───────────────────────────────────────────────────────────┘
                                  (tail committed, session freed)
```

- **hotkey-down (push-to-talk):** validate an input method is active on the seat → open `QAudioSource` → `parakeet_capi_stream_begin` → transition to `LISTENING`. Emit `listeningChanged` so the UI shows the overlay.
- **while listening:** each `QAudioSource::readyRead` chunk is resampled and passed to `parakeet_capi_stream_feed` **on a dedicated worker thread** (Qt `QThread` + `QMetaObject::invokeMethod` / a lock-free ring buffer) to keep the GUI thread responsive. Newly finalized text is forwarded to `TextCommitter`.
- **on `<EOU>`:** the current finalized text is committed; preedit is cleared (sentence boundary).
- **hotkey-up:** stop capture → `parakeet_capi_stream_finalize` → commit the tail → `parakeet_capi_stream_free` → back to `IDLE`.
- **cancel (Esc):** discard any uncommitted preedit, free the session, return to `IDLE`.

Threading invariant: parakeet calls are **not** thread-safe per context; all `stream_*` calls for one session happen on the same worker thread. The controller owns exactly one session at a time.

### 6.3 Inference backend: CPU + Vulkan via two `libparakeet` variants

parakeet.cpp is built **twice** from the vendored submodule, producing two shared libraries with an **identical flat C-API** surface:

| Variant | CMake | Runtime dependency |
| --- | --- | --- |
| `libparakeet-cpu.so` | `-DPARAKEET_SHARED=ON` | none (portable, `GGML_NATIVE` may be OFF for portability) |
| `libparakeet-vulkan.so` | `-DPARAKEET_SHARED=ON -DPARAKEET_GGML_VULKAN=ON` | Vulkan loader (`libvulkan1`) + a Vulkan-capable GPU |

`ParakeetBackend` `dlopen`s the selected variant at startup and resolves the needed symbols (`parakeet_capi_load/free`, `parakeet_capi_stream_begin/feed/finalize/free`, `parakeet_capi_free_string`, `parakeet_capi_last_error`). This mirrors how **LocalAI** embeds parakeet, isolates the Vulkan dependency (a CPU-only user never loads it), and gives a clean "Backend: CPU | Vulkan" toggle in settings.

- Vulkan auto-selects the first reported device (parakeet's default); unsupported ops transparently fall back to CPU within ggml, so a model always runs.
- If the Vulkan lib fails to load or no device is found, Kea warns and falls back to the CPU variant.
- The model GGUF is loaded once per `parakeet_capi_load` and reused across all sessions (warm load ~ once per process; the worker keeps the context alive).

> **Alternative considered:** a single Vulkan-enabled build that switches device via `PARAKEET_DEVICE`. Rejected for v1 because it couples every install to the Vulkan loader and because the flat C-API exposes no runtime device-select function — dlopen of two specialized libs is cleaner and is the proven pattern. (We may upstream a device-select C-API later; then a single lib becomes viable.)

### 6.4 Audio capture & resampling

`AudioRecorder` wraps `QAudioSource` in pull mode:

```cpp
// format: 48 kHz, mono, Int16  (PipeWire via Qt Multimedia)
m_source = new QAudioSource(QMediaDevices::defaultAudioInput(), fmt, this);
m_io = m_source->start();
connect(m_io, &QIODevice::readyRead, this, &AudioRecorder::onReadyRead);
```

Each chunk: `int16 → float32` (`/32768.0`), accumulate, **linear-resample 48 kHz → 16 kHz** (matching parakeet's own internal linear resampler; optional `libsamplerate` for higher quality later), hand a fixed 16 kHz mono f32 block to the controller's worker thread. A short VAD/silence gate is **not** required for v1 because the EOU model natively detects utterance ends, but a simple RMS noise floor can drive the UI level meter.

### 6.5 Text insertion subsystem (the core novelty)

**Binding the protocol.** Using `qt_generate_wayland_protocol_client_sources` on the vendored `input-method-unstable-v2.xml`, `InputMethod` extends `QWaylandClientExtensionTemplate`:

```cpp
class InputMethod
  : public QWaylandClientExtensionTemplate<InputMethod>,
    public QtWayland::zwp_input_method_v2
{
    // bind zwp_input_method_manager_v2 → get_input_method(seat)
    // handle activate()/deactivate()/done(serial) events
    // expose isActive(), lastSerial()
};
```

**Commit flow** (the double-buffered protocol contract):

1. Wait for `activate()` followed by `done(serial)` — the seat has a focused text field and Kea owns the input method.
2. On each finalized text increment: `set_preedit_string(pendingTail, …)` (live, not-yet-finalized words shown inline) + `commit_string(finalizedText)` + `commit(serial)` using the **latest** `done` serial. Serial mismatch ⇒ silently dropped.
3. On `<EOU>` / hotkey-up: `commit_string(tail)`, clear preedit, `commit(serial)`.

**TextCommitter** owns the pending serial counter and guarantees requests only go out while active and after a matching `done`. It is the only component allowed to talk to `InputMethod`.

### 6.6 Settings & model management

Persisted via `KConfig` (or `QSettings`). Settings window built with Kirigami `FormLayout` / FormCard delegates:

- **Shortcut** — push-to-talk (editable via `KeySequenceItem`, wired to `KGlobalAccel`).
- **Model** — pick from `data/models.json` catalog; default `parakeet_realtime_eou_120m-v1` (q8_0). Includes a downloader (fetch from `mudler/parakeet-cpp-gguf` on HuggingFace, progress bar, checksum verify) into `~/.local/share/kea/models/`.
- **Backend** — `CPU` | `Vulkan`.
- **Audio device** — input device selector (`QMediaDevices::audioInputs()`), default = system default.

### 6.7 UI surfaces

- **System tray** (KStatusNotifierItem): icon reflects state (idle / listening / error); context menu = Start/Stop, Settings, Quit.
- **Settings window** (Kirigami `ApplicationWindow` with `pageStack`): the screens above; `KAboutData` about page.
- **Floating "listening" indicator** (borderless `QQuickWindow`, layer-shell or input-method popup surface): a small pill showing live transcript/preedit so the user gets feedback even when the target field is tiny or offscreen. v1 may stub this as the input-method popup surface; full polish is post-v1.

---

## 7. Build & Packaging

- **Toolchain:** CMake + ECM (KDE's `KDEInstallDirs`/`KDECMakeSettings`/`KDECompilerSettings`), Qt6, KF6 (Kirigami, I18n, CoreAddons, Config, GlobalAccel, StatusNotifierItem, IconThemes), Qt6 Multimedia, Qt6 Wayland (Client), `extra-cmake-modules`.
- **parakeet.cpp:** vendored as a git submodule under `3rdparty/parakeet.cpp`; built as two shared-lib targets (cpu / vulkan) via `add_subdirectory` with the appropriate `-D` flags. `third_party/ggml` comes with the submodule.
- **Wayland protocol:** `input-method-unstable-v2.xml` vendored under `data/protocols`; fed to `qt_generate_wayland_protocol_client_sources`.
- **QML module:** `ecm_add_qml_module(kea URI org.kde.kea)` + `ecm_target_qml_sources`; C++ types registered with `QML_ELEMENT`/`QML_SINGLETON`.
- **Packaging:** produce a Flatpak (preferred for portal/sandbox consistency) and/or an AppImage, plus distro `.desktop` + AppStream metadata. The Vulkan `.so` ships alongside; Flatpak needs Vulkan + PipeWire + `input-method` portal permissions.

---

## 8. Phased Implementation Plan

### Phase 0 — Foundations (≈1 week)
- Repo scaffold: CMake, ECM, Kirigami "hello" window, tray icon, `KAboutData`.
- Vendor parakeet.cpp submodule; build both `.so` variants; `dlopen` loader + a smoke `parakeet_capi_transcribe_path` over a test WAV.

### Phase 1 — Headless pipeline (≈2 weeks)
- `AudioRecorder` (PipeWire via `QAudioSource`) + resampler.
- `ParakeetBackend` streaming wrapper: feed live PCM, log finalized text + EOU to console.
- End-to-end "speak → transcript printed" with no UI insertion yet. Validates latency and RTFx on CPU and Vulkan.

### Phase 2 — Text insertion (≈2–3 weeks, highest risk)
- Bind `input-method-v2`; reach `activate()` + `done()` on a test text field.
- `TextCommitter`: commit finalized text + live preedit; verify in KWrite / Konsole / Firefox.
- Handle serial correctness, activate/deactivate reset, error paths.
- **Gate:** reliable commit into ≥3 real Plasma apps on both XWayland and native Wayland surfaces.

### Phase 3 — Integration & UX (≈1–2 weeks)
- `DictationController` state machine wiring hotkey → capture → inference → commit.
- `KGlobalAccel` push-to-talk; Esc-cancel; tray state; listening overlay stub.
- Settings window (hotkey, backend, device); model downloader.

### Phase 4 — Polish & release (≈1 week)
- Error handling, first-run onboarding (download model, grant mic), logging.
- AppStream metadata, screenshots, Flatpak/AppImage, install/launch testing.

**Out of scope, later phases:** LLM cleanup (Phase 5+), transcript history, snippets, personal dictionary, multilingual model picker, fcitx5 coexistence.

---

## 9. Risks & Mitigations

| # | Risk | Impact | Mitigation |
| --- | --- | --- | --- |
| R1 | **IME slot conflict.** Only one input method per seat; if fcitx5/IBus is running, Kea cannot bind. | High — blocks insertion for IME users. | Detect at startup; show a clear "disable your IME to use Kea dictation" message. Document prominently. v1 targets users without a competing IME (typical for English dictation). Long-term: investigate fcitx5 add-on integration or v3 negotiation. |
| R2 | **Serial/commit correctness** under rapid finalize ticks; mismatched serials silently drop text. | High. | Centralize all commits in `TextCommitter`; strict invariant: never commit without a matching `done`; unit-test the serial counter. |
| R3 | **Wayland surface quirks** — some apps don't implement `text-input-v3`, so the input method never `activate`s. | Medium. | Test a matrix of apps; fall back to a visible "open Kea window to read the transcript" mode when no field is active. |
| R4 | **Vulkan availability/fragility** across drivers. | Medium. | Always ship the CPU variant; auto-fallback on load failure; log device selection. |
| R5 | **Latency on low-end CPU** makes streaming feel laggy. | Medium. | Default model is the small 120M streaming EOU (fast); expose thread count (`parakeet_capi` thread control) and quantization (q8_0/q4_k) in advanced settings. |
| R6 | **KGlobalAccel on Wayland** registration flakiness (historical Plasma 6 issues). | Low–Med. | Use standard `KActionCollection` + `KGlobalAccel`; provide a tray-menu "Start" as an always-available fallback. |
| R7 | **parakeet C-API is per-context non-thread-safe.** | Medium. | Pin all `stream_*` calls for a session to one worker thread; never share a context across threads. |
| R8 | **PipeWire mic permission** in sandboxed (Flatpak) packaging. | Low. | Declare mic + input-method portal access; provide non-Flatpak build for users who hit portal issues. |

---

## 10. Open Questions

1. **Streaming vs. offline model as default.** Streaming EOU 120M gives the best "words appear live" UX, but the offline `parakeet-tdt_ctc-110m` is slightly more accurate per-word. Confirm streaming is the v1 default (recommended).
2. **Floating overlay mechanism.** Use the `zwp_input_popup_surface_v2` (tied to the input method, near the cursor) or an independent layer-shell window? Layer-shell gives more layout freedom but needs `layer-shell-qt`/KWin privilege.
3. **Model downloader hosting/source.** Pull directly from HuggingFace `mudler/parakeet-cpp-gguf`, or mirror/bundle a recommended default?
4. **Quantization default.** q8_0 (WER 0, ~0.39× size) is the safe default; offer q4_k for constrained devices. Confirm q8_0 default.
5. **Packaging target priority.** Flatpak first (sandbox story), or native distro packages first (fewer portal/permission headaches for mic + input method)?

---

## 11. Alternatives Considered

- **Text insertion via `uinput`/`ydotool` (virtual keyboard).** Works alongside any IME and everywhere, but needs a daemon + `input` group, and types keystroke-by-stroke (or clipboard+paste, clobbering the clipboard). Rejected for v1 in favor of the protocol-correct input method; revisit as a fallback mode if R1/R3 bite hard.
- **Text insertion via `wtype` (`virtual-keyboard` protocol).** Rejected — KWin does not support it for arbitrary clients.
- **Single parakeet build + `PARAKEET_DEVICE` runtime switch.** Rejected for v1 (no device-select C-API; couples all installs to Vulkan loader). Revisit if we upstream a device selector.
- **Running parakeet as a separate server process (HTTP).** Rejected — adds latency and a second binary to ship; in-process `dlopen` keeps everything local and fast.
- **Whisper.cpp as the engine.** Rejected — parakeet.cpp is materially faster on CPU and GPU with comparable/better accuracy and offers native streaming + EOU.
- **LLM cleanup at launch.** Deferred — pure ASR first keeps v1 focused and the critical path low-latency; the `TextCommitter`/controller seam makes adding a post-processor a later, low-risk change.

---

## 12. References

- parakeet.cpp — `README.md`, `include/parakeet_capi.h`, `AGENTS.md` (local repo).
- parakeet streaming C-API: `parakeet_capi_stream_begin/feed/finalize/free`, EOU/EOB bitmask (`PARAKEET_EVENT_EOU/EOB`).
- parakeet build flags: `-DPARAKEET_SHARED=ON` (shared lib), `-DPARAKEET_GGML_VULKAN=ON` (Vulkan), `PARAKEET_DEVICE` device selection.
- Models: `mudler/parakeet-cpp-gguf` on HuggingFace (f16/q8_0/q4_k…); streaming model `parakeet_realtime_eou_120m-v1`.
- Kirigami setup (C++): `develop.kde.org/docs/getting-started/kirigami/` (`ecm_add_qml_module`, `loadFromModule`, `Kirigami.ApplicationWindow`).
- Wayland input-method-v2 spec: `wayland.app/protocols/input-method-unstable-v2` (single input method per seat; activate/done/commit_string/set_preedit_string; double-buffered commit).
- Qt6 Wayland client extension: `QWaylandClientExtensionTemplate` + `qt_generate_wayland_protocol_client_sources`.
- Audio: Qt6 Multimedia `QAudioSource` (native PipeWire backend ≥ 6.10).
- Hotkey: `KGlobalAccel` + `KActionCollection`. Tray: `KStatusNotifierItem`.
