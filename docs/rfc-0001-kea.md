# RFC 0001: Kea — Offline Voice Dictation for KDE Plasma

| Field | Value |
| --- | --- |
| **Title** | Kea: On-device voice dictation for Linux / KDE Plasma (Wayland) |
| **Status** | Accepted (reconciled with implementation) |
| **Date** | 2026-07-17 |
| **Reconciled** | 2026-07-18 |
| **Stack** | C++17 · Qt6 / Kirigami · parakeet.cpp (ggml) · PipeWire · Wayland `input-method-unstable-v1` |
| **Target platform** | KDE Plasma 6 on Wayland, PipeWire audio stack |
| **Inspiration** | Wispr Flow — but local-first, open, and native to the Linux desktop |

---

## 1. Abstract

**Kea** is a system-wide voice dictation application for KDE Plasma on Wayland. Place your cursor in any text field, hold a global push-to-talk hotkey, speak, and transcribed text is committed into the focused field — entirely on-device, with no cloud dependency and no audio leaving the process.

Speech recognition is powered by **parakeet.cpp**, the C++/ggml inference port of NVIDIA's Parakeet ASR models. Kea supports **both streaming and offline** models: streaming models feed PCM live and emit finalized increments as you speak; offline models (the default) buffer while the hotkey is held and transcribe on release. Mode is auto-detected from the loaded model.

The UI is a **Kirigami** + QML application with a system-tray presence and a settings window. Audio is captured via **Qt6 Multimedia** (`QAudioSource`) on PipeWire hosts. Transcribed text is injected using the Wayland **`input-method-unstable-v1`** protocol — what KWin implements (not v2).

v1 ships pure speech-to-text with two inference backends — **CPU** and **Vulkan** GPU — user-selectable. An LLM-based cleanup/formatting layer (the Wispr Flow "polish" feature) is deferred, but the architecture leaves a clean seam for it.

---

## 2. Background & Motivation

Wispr Flow proved demand for "anywhere, press-and-speak" dictation with live cleanup. It is macOS/Windows/iOS/Android only and cloud-based. Linux users lack a first-class equivalent: existing options are non-global, non-streaming, accuracy-limited, or require the network.

parakeet.cpp closes the engine gap: NVIDIA Parakeet accuracy faster than real-time on CPU (and faster on GPU), byte-identical to NeMo, with a flat C-API and optional streaming + end-of-utterance (EOU) models. Combined with Kirigami, a private always-on dictation tool is viable on the Linux desktop.

The hard problem is not recognition — it is **injecting text into arbitrary applications on Wayland**, where X11 tricks (`xdotool`, `XSendEvent`) do not work. Kea solves this by registering as a Wayland input method.

---

## 3. Goals & Non-Goals

### Goals (v1)

- **G1.** Global push-to-talk (and optional toggle) hotkey that starts/stops dictation from any focused text field.
- **G2.** On-device transcription with low end-to-end latency. Streaming models show words as they finalize; offline models commit on release. Both are first-class.
- **G3.** On-device inference only — no network calls for recognition, no audio upload, no account. (Model download is optional first-run convenience.)
- **G4.** Two selectable backends: **CPU** and **Vulkan**, chosen in settings.
- **G5.** System-tray icon + a Kirigami settings window (hotkey, model path, backend, activation mode).
- **G6.** Runs on KDE Plasma 6 / Wayland with a PipeWire audio host.
- **G7.** Clean seam for a future LLM post-processing layer without redesign.

### Non-Goals (v1)

- ❌ LLM cleanup / formatting / command mode / rewrite transforms.
- ❌ Transcript history, scratchpad, personal dictionary, snippets.
- ❌ X11 support, PulseAudio-only hosts (no PipeWire), or non-Linux platforms.
- ❌ Multilingual auto-switching as a first-class UI (the default offline TDT model is multilingual; language selection UI is later).
- ❌ Coexistence with an already-running IME (fcitx5/IBus) — see **§9 Risks**.

---

## 4. High-Level Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                            Kea process                                │
│                                                                       │
│   ┌─────────────┐    PCM 16k mono f32    ┌────────────────────────┐  │
│   │  Audio In   │ ─────────────────────▶ │   parakeet.cpp (ggml)  │  │
│   │ QAudioSource│   resample 48k→16k     │   streaming *or*       │  │
│   │ (PipeWire)  │                         │   offline (TDT/etc.)   │  │
│   │ int16→f32   │                         │   backend: CPU|Vulkan  │  │
│   └─────────────┘                         └───────────┬────────────┘  │
│          ▲                                             │ finalized    │
│          │ start/stop                                   │ text         │
│          │                                              ▼              │
│   ┌──────┴───────┐   QML/signals   ┌──────────────────────────────┐  │
│   │  Hotkey      │◀───────────────▶│   Dictation Controller        │  │
│   │ KGlobalAccel │                 │   (+ ParakeetWorker thread)   │  │
│   └──────────────┘                 └───────────────┬──────────────┘  │
│                                                     │ commit_string   │
│   ┌──────────────┐                                  ▼                 │
│   │ System Tray  │                      ┌─────────────────────────┐   │
│   │ KStatusNotif │                      │ Text Insertion (input   │   │
│   │ ierItem      │                      │ method-v1 client)       │   │
│   └──────────────┘                      └────────────┬────────────┘   │
└──────────────────────────────────────────────────────┼────────────────┘
                                    wl_seat / zwp_input_method_v1
                                                        │
                                                        ▼
                                             ┌────────────────────┐
                                             │  KWin (compositor)  │
                                             │  → focused surface  │
                                             └────────────────────┘
```

**Data flow:**
1. User loads a model (Start in the UI) → worker `dlopen`s the selected backend and `parakeet_capi_load`s the GGUF.
2. Hotkey-down → controller opens capture + `beginSession` on the worker.
3. Session mode: if `stream_begin` succeeds → **streaming** (feed PCM live, commit increments); else → **offline** (buffer PCM, transcribe on finalize).
4. Hotkey-up → stop capture → finalize/transcribe tail → commit via `TextCommitter` → free session.
5. Stop in the UI unloads the model so backend/path settings can change.

---

## 5. Core Technology Decisions

| Concern | Choice | Rationale |
| --- | --- | --- |
| UI toolkit | **Kirigami + QML** on Qt6 | Native Plasma look/feel, KDE-recommended. |
| ASR engine | **parakeet.cpp** (streaming + offline) | Flat C-API; streaming with EOU when the model supports it; offline path for TDT/CTC/RNNT models. |
| Inference backends | **CPU** and **Vulkan** (two `libparakeet` variants, runtime `dlopen`) | CPU covers everyone; Vulkan for discrete GPU. |
| Audio capture | **Qt6 Multimedia `QAudioSource`** on PipeWire hosts | Consistent API; no raw PipeWire C in v1. CMake requires Qt ≥ 6.6 (see §5.3). |
| Global hotkey | **KGlobalAccel** via `globalShortcutActiveChanged` | Hold-to-talk needs press *and* release; default **Ctrl+Shift+D** (avoids Meta, which some Plasma setups intercept). |
| Text insertion | **Wayland `input-method-unstable-v1`** | What KWin implements (`InputMethodV1Interface`). Not v2. |
| Tray presence | **KStatusNotifierItem** | Plasma-native; works on Wayland. |
| Settings storage | **`QSettings`** (`kea`/`kea` → `~/.config/kea/kea.conf`) | Simple; no KConfigXT/KCM in v1. |
| Process model | **Single foreground process** | Audio ↔ ASR ↔ insertion stay in-process for latency. |
| parakeet source | **Build-time `ExternalProject_Add`** (`cmake/parakeet.cmake`) | Not a git submodule; clones/builds into the build tree under `build/parakeet/`. |

### 5.1 Dual-mode: streaming and offline

`ParakeetWorker::beginSession()` prefers streaming: if `parakeet_capi_stream_begin` succeeds, PCM is fed live and newly finalized text is emitted as it arrives. If stream begin fails (offline TDT/CTC/RNNT models), the worker buffers PCM and runs a one-shot `transcribePcm` on finalize.

**Default model:** offline **Parakeet TDT 0.6B v3** (`tdt-0.6b-v3-q8_0.gguf`) under `~/.local/share/kea/models/` (override with `$KEA_MODEL`). Multilingual (25 European languages); UX is buffer-while-held, commit-on-release.

**Streaming model (catalog):** `parakeet_realtime_eou_120m-v1` — cache-aware streaming with EOU. Live finalized increments while speaking; `stream_finalize` flushes the tail on release.

Both entries live in `data/models.json`. Mode is **model-driven**, not a separate user toggle.

Streaming C-API shape:

```c
parakeet_ctx    *ctx = parakeet_capi_load("…gguf");
parakeet_stream *s   = parakeet_capi_stream_begin(ctx);
int eou = 0;
char *text = parakeet_capi_stream_feed(s, pcm, n_samples, &eou);  // newly finalized only
char *tail = parakeet_capi_stream_finalize(s);
parakeet_capi_stream_free(s);
```

- Input: **16 kHz mono float32**. Kea resamples capture (typically 48 kHz int16) before feed.
- `stream_feed` returns only text newly finalized since the last call — suitable for commit-as-you-go and (later) live preedit of the unfinished tail.

### 5.2 Why input-method-v1 (not v2)

Wayland clients cannot inject key events into surfaces they do not own. KWin does not expose the `virtual-keyboard` protocol for arbitrary clients (`wtype` fails on Plasma). The protocol-correct path is to be the seat's **input method**.

**KWin implements `input_method_unstable_v1`, not v2.** Kea binds `zwp_input_method_v1` (global). On activate, KWin creates a `zwp_input_method_context_v1`. Serial comes from the context's `commit_state(serial)` event. Commits use `commit_string(serial, text)` and `preedit_string(serial, text, commit)` — not v2's double-buffered `commit_string` + separate `commit(serial)`.

**Cost:** one input method object per seat. Kea occupies that slot and cannot coexist with fcitx5/IBus (§9 R1).

### 5.3 Qt version and PipeWire

CMake requires **Qt 6.6**. Qt Multimedia's **native** PipeWire backend is documented for newer Qt (≈6.10+); on 6.6–6.9 Linux capture may still go through the FFmpeg/PulseAudio-compat path while the host remains PipeWire. Kea targets PipeWire *hosts*; it does not require the native-PW Qt backend. Non-goal remains PulseAudio-**only** systems (no PipeWire).

---

## 6. Detailed Design

### 6.1 Repository & module layout

```
kea/
├── CMakeLists.txt                 # ECM, Qt6 ≥ 6.6, KF6
├── cmake/parakeet.cmake           # ExternalProject: CPU (+ Vulkan) libparakeet.so
├── io.github.su3h7am.kea.desktop
├── io.github.su3h7am.kea.metainfo.xml
├── data/
│   ├── protocols/
│   │   └── input-method-unstable-v1.xml
│   └── models.json                # catalog (default offline TDT + streaming EOU)
└── src/
    ├── main.cpp
    ├── CMakeLists.txt
    ├── app/
    │   ├── Main.qml               # settings + onboarding + status
    │   ├── app_settings.*         # QSettings
    │   ├── model_downloader.*
    │   ├── readiness.*
    │   └── tray_controller.*      # KStatusNotifierItem
    ├── audio/
    │   ├── audio_recorder.*       # QAudioSource
    │   ├── resampler.*            # → 16 kHz mono f32
    │   └── wav_loader.*
    ├── inference/
    │   └── parakeet_backend.*     # dlopen loader; offline + streaming
    ├── hotkey/
    │   └── global_hotkey.*        # KGlobalAccel + activeChanged
    ├── insert/
    │   ├── input_method.*         # v1 bind + context
    │   ├── input_context.h        # IInputContext (test seam)
    │   └── text_committer.*       # only path that commits/preedits
    └── controller/
        ├── dictation_controller.* # state machine
        └── parakeet_worker.*      # QThread; all parakeet calls
```

App ID / QML URI / AppStream id: **`io.github.su3h7am.kea`** (not `org.kde.*` — Kea is not an official KDE project).

### 6.2 The dictation controller (state machine)

`DictationController` drives the pipeline (exposed to QML). States:

```
                    loadModel
  Idle ──────────────────────▶ LoadingModel ──fail──▶ Error / Idle
   │                                │
   │ start (model ready)            ok
   ▼                                │
 Starting ──session ok──▶ Listening ──hotkey-up / stop──▶ Draining ──▶ Idle
   │                         │
   │ fail                    │ cancel (tray)
   ▼                         ▼
 Error / Idle               Idle (session discarded)
```

Also: **`ActivationMode`** — PushToTalk (hold via `activeChanged`) or Toggle (press to start/stop). PTT ignores discrete `triggered` to avoid double start/stop.

Lifecycle details that matter:

- **`start()` enters `Starting` synchronously** so overlapping hotkey events cannot open multiple mic/parakeet sessions.
- **Start/Stop in the UI** load and unload the model; settings that affect the backend (path, CPU/Vulkan) are only editable when the model is not loaded (`canConfigure`).
- **Streaming:** `onTextFinalized` → `TextCommitter::commitText` for each increment; finalize commits the tail.
- **Offline:** no increments; full transcript on `sessionFinished`.
- **Cancel:** `DictationController::cancel()` discards the session (wired from the tray menu). No Esc global shortcut in v1.
- **Preedit:** `TextCommitter::setPreedit` / `clearPreedit` are implemented and unit-tested but the controller does not call them yet (no live inline feedback; see open issues).

Threading invariant: parakeet C-API is **not** thread-safe per context. All stream/transcribe calls for a session run on the worker thread. One session at a time.

### 6.3 Inference backend: CPU + Vulkan via two `libparakeet` variants

parakeet.cpp is built **twice** via `ExternalProject_Add` (when `-DKEA_BUILD_PARAKEET=ON`), producing self-contained shared libraries with an identical flat C-API:

| Variant | Build flag | Runtime dependency |
| --- | --- | --- |
| CPU `libparakeet.so` | `-DPARAKEET_SHARED=ON` | none |
| Vulkan `libparakeet.so` | `-DPARAKEET_SHARED=ON -DPARAKEET_GGML_VULKAN=ON` | Vulkan loader + capable GPU |

Artifacts land under `build/parakeet/{cpu,vulkan}-build/`. `ParakeetBackend` `dlopen`s the selected variant and resolves symbols (load/free, offline transcribe, stream begin/feed/finalize/free, free_string, last_error).

- If Vulkan fails to load, the worker falls back to CPU.
- **Backend switch:** both variants ship private `libggml*.so` with the **same SONAME**. Switching requires `dlclose` of the old variant before `dlopen` of the new one, or the dynamic linker reuses the old ggml mapping. After `dlclose`, ggml's process-global `std::terminate` handler is stale — `unload()` resets it to `std::abort`. This works but is fragile (see §9 R9).

> **Alternative considered:** single Vulkan-enabled build + `PARAKEET_DEVICE`. Rejected for v1 (no device-select C-API; couples every install to the Vulkan loader).

### 6.4 Audio capture & resampling

`AudioRecorder` wraps `QAudioSource` (pull / short poll interval to avoid re-entrancy issues on some backends):

```cpp
// format: typically 48 kHz, mono, Int16
m_source = new QAudioSource(QMediaDevices::defaultAudioInput(), fmt, this);
```

Each chunk: int16 → float32, linear-resample **48 kHz → 16 kHz**, hand mono f32 blocks to the worker. RMS level drives a UI meter. No VAD gate in v1 (streaming EOU models detect ends; offline path uses the full hold window).

### 6.5 Text insertion subsystem

**Binding.** `qt_generate_wayland_protocol_client_sources` on `data/protocols/input-method-unstable-v1.xml`. `InputMethod` extends `QWaylandClientExtensionTemplate` and implements `QtWayland::zwp_input_method_v1`:

```cpp
// bind zwp_input_method_v1 (global)
// activate → new zwp_input_method_context_v1
// context: commit_state(serial), surrounding_text, reset, …
// commit_string(serial, text) / preedit_string(serial, text, fallbackCommit)
```

**Commit flow (v1):**

1. Wait for activate + a live context; track serial from `commit_state`.
2. Finalized text → `TextCommitter::commitText` → `commit_string(serial, text)` while the context is valid.
3. Inactive / destroyed context → skip commit (count + error string); never crash.

**TextCommitter** is the only component that may talk to an `IInputContext`. Serial bookkeeping lives on the Wayland context object; TextCommitter owns skip-when-inactive, preedit idempotency, and test-friendly counting. Unit tests use a mock context (`kea-committer-test`).

### 6.6 Settings & model management

Persisted with **`QSettings("kea", "kea")`**: `modelPath`, `backend` (0=CPU, 1=Vulkan), `hotkey`, `activationMode`, `onboardingDone`.

Settings UI is a single Kirigami page in `Main.qml` (not a multi-page Model Manager yet):

- **Shortcut** — default Ctrl+Shift+D; registered with KGlobalAccel (user can also override in System Settings → Shortcuts → Kea).
- **Model path** — free path + optional download of the default GGUF from HuggingFace (`mudler/parakeet-cpp-gguf`). Catalog file `data/models.json` exists; full catalog browser and **checksum verification are not wired yet**.
- **Backend** — CPU | Vulkan.
- **Activation** — push-to-talk | toggle.

### 6.7 UI surfaces

- **System tray** (KStatusNotifierItem): state-aware; menu includes Start/Stop dictation, Cancel, Settings, Quit.
- **Settings window** (Kirigami `ApplicationWindow`): readiness banner, model path, download, backend, hotkey, activation mode; close-to-tray.
- **Floating "listening" indicator / live preedit:** not implemented. Preedit API exists for a later pass.

---

## 7. Build & Packaging

- **Toolchain:** CMake + ECM, Qt6 ≥ 6.6 (Core, Gui, Qml, Quick, QuickControls2, Multimedia, Network, WaylandClient), KF6 (Kirigami, I18n, CoreAddons, Config, GlobalAccel, StatusNotifierItem, IconThemes).
- **parakeet.cpp:** fetched at build time by `cmake/parakeet.cmake` (`ExternalProject_Add`, pin via `KEA_PARAKEET_GIT_*`). Target `parakeet_all`. GUI-only builds: `-DKEA_BUILD_PARAKEET=OFF`.
- **Wayland protocol:** `input-method-unstable-v1.xml` under `data/protocols`.
- **QML module:** `ecm_add_qml_module(kea URI io.github.su3h7am.kea)` — executable target created **before** the QML module so `main()` is preserved.
- **Tests:** `kea-resampler-test`, `kea-committer-test`, `kea-controller-test`, `kea-parakeet-backend-test`; optional smokes when parakeet is built.
- **Packaging (planned):** Flatpak and/or AppImage; `.desktop` + AppStream metadata already present. Flatpak will need mic + Vulkan + PipeWire permissions.

---

## 8. Implementation status

Phases 0–4 are **scaffolded / largely implemented** (pre-alpha). Mapping to the original plan:

| Area | Status |
| --- | --- |
| CMake, Kirigami shell, tray, about | Done |
| ExternalProject dual `libparakeet` + dlopen loader | Done |
| AudioRecorder + resampler | Done |
| Streaming feed + offline buffer path | Done |
| input-method-v1 + TextCommitter | Done |
| DictationController + ParakeetWorker | Done |
| KGlobalAccel hold-to-talk (+ toggle mode) | Done |
| Settings (QSettings) + model path + backend | Done |
| Default model download | Partial (no integrity verify; catalog not full UI) |
| Cancel | Tray menu only (no Esc binding) |
| Live preedit / floating listening indicator | Not done (API only for preedit) |
| Flatpak/AppImage | Not done |
| Esc-cancel, multi-page model manager | Not done |

**Later phases (unchanged intent):** LLM cleanup/transforms, transcript history, snippets, personal dictionary, richer multilingual UI, fcitx5 coexistence.

---

## 9. Risks & Mitigations

| # | Risk | Impact | Mitigation |
| --- | --- | --- | --- |
| R1 | **IME slot conflict.** One input method per seat; fcitx5/IBus blocks Kea. | High | Detect bind failure; readiness UI warns. Document. Long-term: fcitx5 add-on or protocol evolution. |
| R2 | **Serial/context correctness.** Commits against a dead context or stale serial drop or mis-order text. | High | All commits via `TextCommitter`; skip when `IInputContext` invalid; unit-test commit/preedit; serial owned by context from `commit_state`. |
| R3 | **Client quirks.** Some surfaces never activate a text-input / input-method context (e.g. certain terminals). | Medium | Test matrix; readiness when no context; optional fallback modes later. |
| R4 | **Vulkan fragility** (drivers, RADV + Qt Quick crashes observed on some setups). | Medium | Ship CPU always; auto-fallback on load; log a switch-to-CPU hint when Vulkan is selected. |
| R5 | **Latency / quality tradeoff.** Streaming small models vs offline larger multilingual models. | Medium | Dual-mode; default offline TDT for quality/languages; streaming model in catalog for live UX. |
| R6 | **Global hotkey reliability on Wayland.** Meta interception; need press+release for PTT. | Low–Med | Use `KGlobalAccel::globalShortcutActiveChanged`; default Ctrl+Shift+D; tray Start/Stop fallback. |
| R7 | **parakeet C-API not thread-safe per context.** | Medium | Single worker thread for all parakeet calls; one session. |
| R8 | **Sandboxed mic / protocol permissions** (Flatpak). | Low | Portal declarations when packaging; native package alternative. |
| R9 | **CPU↔Vulkan `dlclose` / ggml SONAME collision.** Dual libs share SONAMEs; stale terminate handler after unload. | Medium | Documented unload/load ordering + `std::set_terminate(std::abort)`. Alternative: require app restart to switch backends (simpler, deletes the dance). |

---

## 10. Open Questions

1. ~~**Streaming vs. offline as default.**~~ **Resolved:** dual-mode is intentional; default catalog/path is offline TDT 0.6B v3; streaming EOU remains first-class when that model is selected.
2. **Floating overlay mechanism.** Layer-shell window vs input-panel surface under v1 — undecided; preedit-in-field is the other feedback path.
3. **Model downloader integrity.** Checksums / signature for GGUF downloads; wire `models.json` into a real picker UI.
4. **Quantization default.** q8_0 remains the safe default; offer smaller quants later for constrained devices.
5. **Packaging priority.** Flatpak first vs native distro packages (mic + input-method permissions differ).
6. **Backend switch UX.** Keep runtime `dlclose` switch vs "restart to apply" (see R9).

---

## 11. Alternatives Considered

- **Text insertion via `uinput`/`ydotool`.** Works alongside IMEs but needs privileges and types keystroke-by-keystroke (or clipboard paste). Rejected for v1; revisit as fallback if R1/R3 bite hard.
- **Text insertion via `wtype` (`virtual-keyboard`).** Rejected — KWin does not support it for arbitrary clients.
- **input-method-v2.** Not implemented by KWin for this use case; Kea targets v1.
- **Single parakeet build + `PARAKEET_DEVICE`.** Rejected for v1 (no C-API device select; Vulkan loader coupling).
- **parakeet as a separate HTTP server.** Rejected — latency and packaging cost; in-process `dlopen` preferred.
- **Whisper.cpp as the engine.** Rejected — parakeet is faster on CPU/GPU with streaming+EOU support.
- **Git submodule for parakeet.cpp.** Rejected in favor of build-time ExternalProject (isolated dual builds, clean source tree).
- **LLM cleanup at launch.** Deferred — pure ASR first; controller/`TextCommitter` seam keeps a later transform stage low-risk.
- **KConfigXT / KCM settings.** Deferred; `QSettings` is enough for v1.

---

## 12. References

- parakeet.cpp — upstream repo; C-API in `include/parakeet_capi.h` (fetched under `build/parakeet/*-src/` when built).
- Streaming API: `parakeet_capi_stream_begin/feed/finalize/free`, EOU/EOB bitmask.
- Build flags: `-DPARAKEET_SHARED=ON`, `-DPARAKEET_GGML_VULKAN=ON`.
- Models: HuggingFace `mudler/parakeet-cpp-gguf` — default offline `tdt-0.6b-v3-q8_0.gguf`; streaming `parakeet_realtime_eou_120m-v1`.
- Kea contributor notes: `AGENTS.md` (invariants; preferred over outdated memory of this RFC).
- Wayland `input-method-unstable-v1` (vendored XML); KWin `InputMethodV1Interface`.
- Qt6: `QWaylandClientExtensionTemplate`, `qt_generate_wayland_protocol_client_sources`, Multimedia `QAudioSource`.
- Hotkey: `KGlobalAccel` (`globalShortcutActiveChanged`). Tray: `KStatusNotifierItem`.
