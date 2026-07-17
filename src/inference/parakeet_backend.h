/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * ParakeetBackend — runtime loader for the parakeet.cpp shared library.
 *
 * parakeet.cpp is built twice (CPU and Vulkan) with an identical flat C-API
 * (include/parakeet_capi.h in the upstream repo). Kea picks which variant to
 * load at runtime via dlopen, resolving the needed symbols from that single
 * shared object. This isolates the Vulkan dependency: a CPU-only system never
 * loads the Vulkan variant.
 *
 * Phase 0: this wires the load + a single-clip offline transcribe path so the
 * build can be smoke-tested end to end. The streaming entry points
 * (stream_begin/feed/finalize) are added in Phase 1.
 */
#pragma once

#include <QString>
#include <functional>
#include <memory>

namespace kea {

/// Which backend variant to load.
enum class ParakeetDevice {
    Cpu,
    Vulkan,
};

/// Result of a single transcription call.
struct TranscriptionResult {
    bool ok = false;
    QString text;          ///< transcript on success, error message on failure
};

/// Result of one streaming feed: newly-finalized text + the EOU/EOB event mask.
struct StreamFeedResult {
    bool ok = false;
    QString text;          ///< newly-finalized text ("" if none this call)
    int eouMask = 0;       ///< PARAKEET_EVENT_EOU | PARAKEET_EVENT_EOB bitmask
    QString error;         ///< set when ok == false
};

// EOU/EOB event bits (match parakeet_capi.h). Defined here so callers don't
// need the upstream header.
constexpr int kParakeetEventEou = 1; // end of utterance
constexpr int kParakeetEventEob = 2; // end of backchannel

/// Opaque parakeet context (parakeet_ctx*), wrapped so callers don't see the
/// C API types directly. Owned by ParakeetBackend.
class ParakeetCtxHandle;

/// Loads one parakeet.cpp variant by .so path and exposes its C-API. Each
/// instance owns one dlopen handle + one model context (parakeet_ctx).
class ParakeetBackend
{
public:
    ParakeetBackend();
    ~ParakeetBackend();

    ParakeetBackend(const ParakeetBackend &) = delete;
    ParakeetBackend &operator=(const ParakeetBackend &) = delete;

    /// Resolve the default .so path for a device variant (build-time path,
    /// baked in by CMake; the Vulkan path is empty when Vulkan was skipped).
    static QString libraryPath(ParakeetDevice device);

    /// dlopen the variant and resolve symbols. Returns false on failure
    /// (sets lastError()).
    bool load(ParakeetDevice device);

    /// Load a model GGUF into the backend's context. Returns false on failure.
    bool loadModel(const QString &ggufPath);

    /// Transcribe a WAV file (offline path, default decoder). Phase 0 smoke.
    TranscriptionResult transcribePath(const QString &wavPath);

    // --- Streaming (parakeet_realtime_eou_120m-v1) ---
    // One active session at a time. All stream_* calls must happen on the same
    // thread (parakeet's C-API is per-context non-thread-safe).

    /// Open a streaming session over the loaded model. `lang` selects the
    /// language prompt for multilingual models ("en"/"de"/"auto"; empty = model
    /// default). Returns false if not a streaming model.
    bool streamBegin(const QString &lang = {});

    /// Feed a block of 16 kHz mono float PCM. Returns the text finalized since
    /// the last call plus any EOU/EOB event that fired.
    StreamFeedResult streamFeed(const float *pcm, int nSamples);

    /// Flush the end-of-stream tail. Returns the final newly-finalized text.
    StreamFeedResult streamFinalize();

    /// Free the active streaming session (no-op if none).
    void streamEnd();

    bool hasStream() const { return m_stream != nullptr; }

    bool isLoaded() const { return m_handle != nullptr; }
    bool hasModel() const { return m_ctx != nullptr; }
    QString lastError() const { return m_lastError; }

private:
    void setError(const QString &msg);

    void *m_handle = nullptr;   ///< dlopen handle
    ParakeetCtxHandle *m_ctx = nullptr; ///< parakeet_ctx (opaque)
    struct parakeet_stream *m_stream = nullptr; ///< active streaming session
    QString m_lastError;

    // Resolved function pointers (set by load()).
    struct Symbols;
    std::unique_ptr<Symbols> m_sym;
};

} // namespace kea
