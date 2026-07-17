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

    bool isLoaded() const { return m_handle != nullptr; }
    bool hasModel() const { return m_ctx != nullptr; }
    QString lastError() const { return m_lastError; }

private:
    void setError(const QString &msg);

    void *m_handle = nullptr;   ///< dlopen handle
    ParakeetCtxHandle *m_ctx = nullptr; ///< parakeet_ctx (opaque)
    QString m_lastError;

    // Resolved function pointers (set by load()).
    struct Symbols;
    std::unique_ptr<Symbols> m_sym;
};

} // namespace kea
