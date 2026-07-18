# RFC 0001: Kea — On-device Voice Dictation for KDE Plasma

| Field | Value |
| --- | --- |
| **Title** | Kea: On-device voice dictation for Linux / KDE Plasma (Wayland) |
| **Status** | Design target (product architecture) |
| **Date** | 2026-07-17 |
| **Stack** | C++17 · Qt6 / Kirigami · parakeet.cpp (ggml) · PipeWire · Wayland input-method (KWin) |
| **Target platform** | KDE Plasma 6 on Wayland, PipeWire audio stack |
| **Inspiration** | Wispr Flow — but local-first, open, and native to the Linux desktop |

### How to read this RFC

This document is the **product and architecture goal** — what Kea should be — not a changelog of the tree on any given day.

- **Goals, decisions, and non-goals** in §§1–7 and §9–11 define the intended design. Prefer the best solution that actually works on the target platform (Plasma 6 / KWin / PipeWire).
- **§8 Implementation status** is the only place that tracks “what ships today vs. still open.” Gaps in §8 are work items, not reasons to rewrite the design downward to match unfinished code.
- When implementation learns a hard platform fact (e.g. which input-method protocol KWin exposes), update the **goal** so it stays correct — do not keep a prettier protocol name that cannot work on Plasma.

---

## 1. Abstract

**Kea** is a system-wide voice dictation application for KDE Plasma on Wayland. Place your cursor in any text field, activate a global push-to-talk (or toggle) hotkey, speak, and transcribed text lands in the focused field — entirely on-device, with no cloud dependency and no audio leaving the process.

Speech recognition is powered by **parakeet.cpp**, the C++/ggml port of NVIDIA Parakeet. The product supports **streaming** models (live finalized text while speaking, with end-of-utterance detection) and **offline** models (buffer during the session, transcribe when it ends). Streaming is the primary “words appear as you speak” experience; offline models remain first-class for multilingual accuracy and larger TDT checkpoints. Mode is selected by the loaded model (auto-detect), not a separate engine switch.

The UI is **Kirigami** + QML with a system-tray presence and settings. Audio is captured via **Qt6 Multimedia** on PipeWire hosts. Text is injected by registering as the seat’s **Wayland input method**, using the protocol **KWin actually implements** (`input_method_unstable_v1` — see §5.2).

Product ships pure speech-to-text with **CPU** and **Vulkan** inference backends. An LLM cleanup / translate / rewrite layer (Wispr-style polish) is a later phase but must not require redesigning the commit seam (see open design issues).

---

## 2. Background & Motivation

Wispr Flow proved demand for “anywhere, press-and-speak” dictation with live cleanup. It is closed-source, cloud-oriented, and not on Linux. Existing Linux options are non-global, non-streaming, accuracy-limited, or network-bound.

parakeet.cpp closes the engine gap: NeMo-parity accuracy, faster than real-time on CPU, faster on GPU, flat C-API, and optional streaming + EOU models. Kirigami gives a native Plasma UI.

The hard problem is not recognition — it is **injecting text into arbitrary applications on Wayland**, where X11 tricks (`xdotool`, `XSendEvent`) do not work. Kea solves this by being a real input method under KWin.

---

## 3. Goals & Non-Goals

### Goals (product)

- **G1.** Global activation (push-to-talk and toggle) from any focused text field.
- **G2.** Low-latency on-device transcription. **Primary UX:** streaming models finalize text live while the user speaks. Offline models remain fully supported (buffer during the session, commit when it ends) for quality and multilingual workloads.
- **G3.** On-device inference only for recognition — no audio upload, no account. Optional model download is first-run convenience only.
- **G4.** Selectable backends: **CPU** and **Vulkan**.
- **G5.** System tray + Kirigami settings (hotkey, model, backend, activation mode); KDE-native control patterns where they exist (e.g. key-sequence capture).
- **G6.** Target: KDE Plasma 6 / Wayland / PipeWire.
- **G7.** Clean seam for future post-processing (cleanup, rewrite, translate) without redesigning insertion.
- **G8.** Live feedback while dictating: inline **preedit** for not-yet-committed text and/or a floating listening indicator so the user is never “flying blind.”
- **G9.** Reliable insertion into the focused client via the compositor’s input-method path; clear UX when no compatible text field is active (never silent drop without feedback).

### Non-goals (initial product)

- ❌ LLM cleanup / command mode / rewrite as day-one requirements (designed later; seam reserved).
- ❌ Full transcript history / scratchpad / personal dictionary / snippets as day-one (designed later).
- ❌ X11, PulseAudio-only hosts, non-Linux.
- ❌ Coexistence with a second seat IME (fcitx5/IBus) in the first product cut — see §9 R1.
- ❌ Multilingual auto-switch UI as a separate product surface (engine/models may already be multilingual).

---

## 4. High-Level Architecture

```text
┌──────────────────────────────────────────────────────────────────────┐
│                            Kea process                                │
│                                                                       │
│   ┌─────────────┐    PCM 16k mono f32    ┌────────────────────────┐  │
│   │  Audio In   │ ─────────────────────▶ │   parakeet.cpp (ggml)  │  │
│   │ QAudioSource│   resample 48k→16k     │   streaming and/or     │  │
│   │ (PipeWire)  │                         │   offline models       │  │
│   │ int16→f32   │                         │   backend: CPU|Vulkan  │  │
│   └─────────────┘                         └───────────┬────────────┘  │
│          ▲                                             │ text + EOU    │
│          │ start/stop                                   ▼              │
│   ┌──────┴───────┐   QML/signals   ┌──────────────────────────────┐  │
│   │  Hotkey      │◀───────────────▶│   Dictation Controller        │  │
│   │ KGlobalAccel │                 │   (+ worker thread)           │  │
│   └──────────────┘                 └───────────────┬──────────────┘  │
│                                                     │ commit/preedit  │
│   ┌──────────────┐                                  ▼                 │
│   │ System Tray  │                      ┌─────────────────────────┐   │
│   │ KStatusNotif │                      │ Text insertion          │   │
│   │ ierItem      │                      │ (input-method client)   │   │
│   └──────────────┘                      └────────────┬────────────┘   │
└──────────────────────────────────────────────────────┼────────────────┘
                              seat input method (KWin)  │
                                                        ▼
                                             ┌────────────────────┐
                                             │  KWin → focused    │
                                             │  text-input client │
                                             └────────────────────┘
```

**Intended data flow:**

1. Load model into the worker (backend `dlopen` + GGUF load).
2. Activation start → open capture + ASR session.
3. If the model supports streaming → feed PCM live, emit finalized increments (and show unfinished text as preedit). Else → buffer PCM for offline decode when the session ends.
4. Activation stop → finalize / offline transcribe → commit tail → free session.
5. Cancel discards uncommitted state without leaving partial garbage in the field.

---

## 5. Core Technology Decisions

| Concern | Choice | Rationale |
| --- | --- | --- |
| UI toolkit | **Kirigami + QML** on Qt6 | Native Plasma look/feel. |
| ASR engine | **parakeet.cpp** | Best fit for on-device Linux: flat C-API, streaming+EOU, offline TDT, CPU RTFx. |
| Inference backends | **CPU** + **Vulkan** (two specialized `libparakeet` variants, runtime select) | Covers everyone; isolates Vulkan dependency. |
| Audio capture | **Qt6 Multimedia `QAudioSource`** on PipeWire hosts | One API; no raw PipeWire C required for the first product. Prefer newer Qt when it brings a native PW backend. |
| Global hotkey | **KGlobalAccel** with press/release via `globalShortcutActiveChanged` | Plasma-native; required for true hold-to-talk. Default sequence avoids Meta interception. |
| Text insertion | **Wayland input method as implemented by KWin** → **`input_method_unstable_v1`** | Only permission-free, protocol-correct insertion path on Plasma Wayland. See §5.2. |
| Tray | **KStatusNotifierItem** | Plasma-native on Wayland. |
| Settings | Prefer simple, reliable persistence; **KConfigXT / KCM** when System Settings integration is worth the cost | First cut may use `QSettings`; long-term goal is KDE-native config surfaces where it helps users. |
| Process model | **Single process** (tray + settings + pipeline) | Lowest latency; one seat input-method owner. |
| parakeet integration | **Build-time fetch** of upstream (`ExternalProject`) into dual shared libs | Clean tree; isolated CPU/Vulkan builds; pin via git tag. |

### 5.1 Streaming and offline (both intentional)

**Streaming (primary live UX).** Cache-aware streaming models with EOU (e.g. `parakeet_realtime_eou_120m-v1`) feed 16 kHz mono f32 PCM and return only newly finalized text plus an EOU/EOB mask. That is the path for “words appear as you speak,” live preedit of the unfinished tail, and natural sentence boundaries.

**Offline.** TDT/CTC/RNNT models that reject `stream_begin` buffer PCM for the session and run a one-shot transcribe on finalize. Higher multilingual quality (e.g. TDT 0.6B v3, 25 European languages) at the cost of no mid-utterance commits.

**Mode selection:** auto-detect from the model (`stream_begin` success → streaming; else offline). Users pick a model; they do not pick a second “engine mode” toggle.

**Default model (product decision):** favor the best out-of-box accuracy/language coverage for a first install when size is acceptable; keep a clearly recommended streaming model in the catalog for live UX. Catalog and downloader must make both discoverable (see §6.6).

Streaming API shape (authoritative upstream C-API):

```c
parakeet_ctx    *ctx = parakeet_capi_load("…gguf");
parakeet_stream *s   = parakeet_capi_stream_begin(ctx);
int eou = 0;
char *text = parakeet_capi_stream_feed(s, pcm, n_samples, &eou);  // newly finalized only
char *tail = parakeet_capi_stream_finalize(s);
parakeet_capi_stream_free(s);
```

### 5.2 Text insertion: why KWin’s input-method protocol (v1), not “newest protocol number”

Wayland clients cannot inject keys into other surfaces. On Plasma, `virtual-keyboard` is not available to arbitrary apps (`wtype` fails). The correct design is: **be the seat’s input method** and commit strings into the focused text-input client — the same class of solution fcitx5 uses.

**Platform fact (this sets the goal, not a temporary compromise):** KWin’s input-method stack is **`input_method_unstable_v1`** (`InputMethodV1Interface`). It does **not** implement `input-method-unstable-v2` for this path. Therefore the product target on Plasma is v1:

| | `input_method_unstable_v1` (KWin) | `input-method-v2` |
| --- | --- | --- |
| On Plasma | **Implemented** | **Not available from KWin** |
| Object model | Global IM → per-focus **context** | Manager → seat IM object |
| Serial | `commit_state(serial)` on the context | `done(serial)` + separate `commit` |
| Commit | `commit_string(serial, text)` | Double-buffered `commit_string` + `commit(serial)` |
| Preedit | `preedit_string(serial, text, commit)` | `set_preedit_string` then `commit` |

“Use v2 because the number is higher” is not a better design on Plasma — it is a design that **cannot bind**. The original draft of this RFC named v2 incorrectly. The **goal** is reliable compositor-backed insertion; on KWin that means v1 until KWin ships another protocol and we re-evaluate.

**Cost (goal-level constraint):** one input method per seat. Kea owns that slot while running; coexistence with fcitx5/IBus is out of scope for the first cut (§9 R1).

### 5.3 Qt version and PipeWire

Minimum Qt tracks what distros and KF6 need (currently ≥ 6.6 is acceptable). Prefer newer Qt when Multimedia’s native PipeWire backend improves capture quality/latency. Host requirement remains **PipeWire**, not PulseAudio-only systems.

---

## 6. Detailed Design

### 6.1 Module layout (target shape)

Logical modules (names may grow; responsibilities should not blur):

```text
kea/
├── CMakeLists.txt / cmake/parakeet.cmake
├── io.github.su3h7am.kea.desktop + AppStream metainfo
├── data/
│   ├── protocols/input-method-unstable-v1.xml
│   └── models.json                 # catalog: ids, URLs, hashes, streaming flag
└── src/
    ├── app/          # UI shell, settings, tray, readiness, model download
    ├── audio/        # capture + resample → 16 kHz mono f32
    ├── inference/    # dlopen parakeet backends
    ├── hotkey/       # KGlobalAccel
    ├── insert/       # input-method client + TextCommitter only
    └── controller/   # state machine + worker thread
```

App ID / QML URI / AppStream: **`io.github.su3h7am.kea`** (not `org.kde.*` — Kea is built *for* Plasma, not as an official KDE project).

### 6.2 Dictation controller

Central state machine (names indicative):

```text
 Idle ──load──▶ LoadingModel ──▶ (ready)
 Idle/ready ──activation start──▶ Starting ──▶ Listening
 Listening ──activation stop──▶ Draining ──▶ Idle
 any busy ──cancel──▶ Idle (discard)
 failures ──▶ Error (recoverable → Idle)
```

**Activation modes:** PushToTalk (hold) and Toggle. Session lifecycle is mode-neutral: “session active” / “session end,” not only key-up.

**Invariants:**

- Enter “starting” synchronously so double activation cannot open two sessions.
- All parakeet calls for a context on one worker thread (C-API is not thread-safe per context).
- All commits/preedits go through **TextCommitter** only.
- Cancel clears preedit and frees the session without committing junk.
- When commit is impossible (no context), surface that to the user (G9) — never look successful while dropping text.

### 6.3 Inference backends

Build two `libparakeet` shared libraries (CPU; Vulkan when available) with an identical C-API. Runtime selection via `dlopen`. CPU is always the fallback.

**Backend switch:** changing CPU ↔ Vulkan is rare. Preferred robust design: load at most one variant per process lifetime and **restart to apply** if process-global ggml state makes `dlclose` unsafe. If runtime switch is kept, it must fully unload the previous variant (SONAME collision on private `libggml*.so`) and not leave stale process-global handlers — but restart-to-apply remains the simpler product default.

### 6.4 Audio

Capture via `QAudioSource`, convert to 16 kHz mono f32 for parakeet. Level meter from RMS. Optional higher-quality resampler later; linear is acceptable for the first product.

### 6.5 Text insertion

Bind KWin’s `zwp_input_method_v1`. On activate, own a `zwp_input_method_context_v1`. Track serial from `commit_state`. Expose an `IInputContext` seam so TextCommitter and tests never talk to Wayland directly.

**Commit path (goal):**

1. Active context + known serial.
2. Finalized ASR text → `commit_string`.
3. Unfinished / gated text → `preedit_string` (live feedback; also the seam for future transform-before-commit).
4. On session end: commit tail, clear preedit.

### 6.6 Settings & models

- Hotkey, backend, activation mode, model path.
- **Model catalog** (`models.json`): name, description, size, streaming vs offline, download URL, **integrity hash**.
- Downloader: progress, cancel, verify hash before promoting the file into place.
- First-run onboarding: model present, input method bound, mic usable.

### 6.7 UI surfaces

- Tray: state, start/stop, cancel, settings, quit.
- Settings window: readiness, model management, backend, hotkey (prefer **KeySequenceItem**-class capture over free-typed strings).
- **Live feedback:** preedit in the focused field and/or floating listening pill (layer-shell or input-panel — open detail).

---

## 7. Build & Packaging

- CMake + ECM, Qt6, KF6 (Kirigami, I18n, CoreAddons, Config, GlobalAccel, StatusNotifierItem, IconThemes, …).
- parakeet via build-time ExternalProject; `KEA_BUILD_PARAKEET=OFF` for UI-only iteration.
- Protocol XML vendored for the KWin input-method interface in use.
- Package: native distro packages and/or Flatpak/AppImage; AppStream + `.desktop` required. Sandbox needs mic + GPU + input-method-relevant permissions.

---

## 8. Implementation status

Snapshot of the tree relative to the goals above. **Update this section when shipping; do not silently lower goals in §§1–7 to match unfinished work.**

| Goal area | Status (pre-alpha) |
| --- | --- |
| Kirigami shell, tray, about | Present |
| Dual libparakeet + dlopen | Present |
| Audio + resampler | Present |
| Streaming + offline sessions | Present |
| input-method-v1 + TextCommitter | Present |
| Controller + worker + PTT/toggle | Present |
| Model download | Partial (no hash verify; catalog not fully UI-driven) |
| Live preedit wired from controller | Missing (API exists) |
| Floating listening indicator | Missing |
| Esc-cancel binding | Missing (tray cancel exists) |
| Silent commit failure UX (G9) | Partial / weak |
| Flatpak/AppImage | Missing |
| KConfigXT / KCM | Not started (`QSettings` in tree) |
| KeySequenceItem hotkey capture | Not started (free-text field) |
| LLM transform pipeline | Design only (separate issue) |
| Transcript history | Design only (separate issue) |

---

## 9. Risks & Mitigations

| # | Risk | Impact | Mitigation |
| --- | --- | --- | --- |
| R1 | One IM per seat; conflict with fcitx5/IBus | High | Detect bind failure; clear readiness copy; document. Long-term: integration or protocol evolution. |
| R2 | Serial / context races drop or reorder text | High | TextCommitter-only commits; skip when invalid; tests; log and surface failures (G9). |
| R3 | Clients that never activate a usable text-input / IM context (some terminals) | Medium | Diagnostics; app matrix; user-visible fallback (history/clipboard-opt-in) only with explicit UX. |
| R4 | Vulkan/driver fragility | Medium | CPU always available; fallback; honest settings hints. |
| R5 | Latency vs quality | Medium | Dual-mode models; catalog guidance; expose threads/quant later. |
| R6 | Global hotkey reliability on Wayland | Low–Med | `activeChanged` for hold; avoid Meta defaults; tray fallback. |
| R7 | parakeet C-API thread safety | Medium | Single worker thread per context. |
| R8 | Sandbox permissions (Flatpak) | Low | Declare portals; offer native package. |
| R9 | Dual-backend `dlclose` / ggml process-global state | Medium | Prefer restart-to-apply for backend switch; if runtime switch remains, document SONAME + terminate-handler constraints. |

---

## 10. Open Questions

1. **Default model recommendation** in the catalog/onboarding (streaming EOU vs offline TDT) — both must remain one-click installable.
2. **Floating overlay** mechanism (layer-shell vs input-panel) vs relying primarily on preedit.
3. **Model integrity** scheme (SHA-256 in `models.json` vs signed manifests).
4. **Packaging priority** (Flatpak vs native).
5. **Backend switch UX** (restart-to-apply vs runtime `dlclose`).
6. **Post-processing presets** and multi-hotkey vs single hotkey (design issue #6).
7. **History UX** after comparable-app research (design issue #7).

---

## 11. Alternatives Considered

- **`uinput` / ydotool / clipboard-paste fallbacks.** Useful as *explicit* fallbacks for R3; not the primary path (permissions, paste safety, clipboard clobber).
- **`wtype` / virtual-keyboard.** Rejected on Plasma for arbitrary clients.
- **Targeting input-method-v2 as the Plasma goal.** Rejected while KWin does not implement it; would not bind. Revisit if KWin adds v2/v3.
- **Single Vulkan `libparakeet` + env device select.** Rejected until a real C-API device selector exists.
- **Separate ASR HTTP process.** Rejected for latency and packaging complexity.
- **Whisper.cpp as primary engine.** Rejected for CPU/GPU speed and streaming+EOU fit.
- **Git submodule for parakeet.** Build-time ExternalProject preferred for dual isolated builds.
- **LLM polish on day one.** Deferred; reserve TextCommitter/preedit seam.

---

## 12. References

- parakeet.cpp C-API (`parakeet_capi_stream_*`, offline transcribe); models on HuggingFace `mudler/parakeet-cpp-gguf`.
- KWin `InputMethodV1Interface` / `input_method_unstable_v1` (vendored XML under `data/protocols/`).
- Qt Wayland client extensions; Qt Multimedia `QAudioSource`.
- `KGlobalAccel`, `KStatusNotifierItem`, Kirigami.
- Contributor invariants: `AGENTS.md` (operational “do not regress”; this RFC remains the product design target).
