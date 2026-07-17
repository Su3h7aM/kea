/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-fake-parakeet-capi — a minimal stand-in for parakeet.cpp's flat C-API
 * (upstream include/parakeet_capi.h), built as a real shared object so
 * kea-parakeet-backend-test can exercise ParakeetBackend's dlopen loader
 * without fetching/building the real (heavyweight, network-dependent)
 * parakeet.cpp dependency.
 *
 * The fake follows the pinned upstream contract for capi_last_error(): a null
 * context returns an empty string. This keeps loader tests representative of
 * the library Kea actually ships against.
 *
 * Only the symbols ParakeetBackend::load() requires to succeed are given
 * real behavior (capi_load/capi_free/capi_last_error); the transcribe
 * entry points are present (load() checks for their existence) but are not
 * exercised by the test and simply return null.
 */
#include <cstdlib>
#include <fstream>
#include <string>

#if defined(__GNUC__)
#define KEA_FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define KEA_FAKE_EXPORT extern "C"
#endif

// Opaque from ParakeetBackend's point of view.
struct parakeet_ctx {
    std::string lastError;
};

namespace {

bool fileExists(const char *path)
{
    if (!path) {
        return false;
    }
    std::ifstream f(path);
    return f.good();
}

} // namespace

KEA_FAKE_EXPORT int parakeet_capi_abi_version()
{
    return 1;
}

KEA_FAKE_EXPORT parakeet_ctx *parakeet_capi_load(const char *path)
{
    // Simulate the real loader: a missing/unreadable model file fails to
    // load (returns null) with no live context to report an error through —
    // exactly the case ParakeetBackend::loadModel() must report cleanly.
    if (!fileExists(path)) {
        return nullptr;
    }
    return new parakeet_ctx();
}

KEA_FAKE_EXPORT void parakeet_capi_free(parakeet_ctx *ctx)
{
    delete ctx;
}

KEA_FAKE_EXPORT char *parakeet_capi_transcribe_path(parakeet_ctx * /*ctx*/, const char * /*path*/,
                                                     int /*decoder*/)
{
    // Not exercised by kea-parakeet-backend-test; present only so
    // ParakeetBackend::load()'s symbol-presence check succeeds.
    return nullptr;
}

KEA_FAKE_EXPORT char *parakeet_capi_transcribe_pcm(parakeet_ctx * /*ctx*/, const float * /*samples*/,
                                                    int /*nSamples*/, int /*sampleRate*/,
                                                    int /*decoder*/)
{
    return nullptr;
}

KEA_FAKE_EXPORT void parakeet_capi_free_string(char *s)
{
    std::free(s);
}

KEA_FAKE_EXPORT const char *parakeet_capi_last_error(parakeet_ctx *ctx)
{
    return ctx ? ctx->lastError.c_str() : "";
}
