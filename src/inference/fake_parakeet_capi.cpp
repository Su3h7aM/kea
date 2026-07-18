/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-fake-parakeet-capi — a minimal stand-in for parakeet.cpp's flat C-API
 * (upstream include/parakeet_capi.h), built as a real shared object so
 * kea-parakeet-backend-test / kea-parakeet-worker-test can exercise loaders
 * without fetching/building the real (heavyweight, network-dependent)
 * parakeet.cpp dependency.
 *
 * Streaming vs offline:
 *   - By default stream_begin returns null → offline (buffer + transcribe_pcm).
 *   - Set env KEA_FAKE_STREAMING=1 to make stream_begin succeed and return
 *     canned feed/finalize text (for ParakeetWorker streaming tests).
 *
 * The fake follows the pinned upstream contract for capi_last_error(): a null
 * context returns an empty string.
 */
#include <cstdlib>
#include <cstring>
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

struct parakeet_stream {
    parakeet_ctx *ctx = nullptr;
    int feedCount = 0;
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

bool streamingEnabled()
{
    const char *e = std::getenv("KEA_FAKE_STREAMING");
    return e && e[0] == '1' && e[1] == '\0';
}

char *dupCString(const char *s)
{
    if (!s) {
        return nullptr;
    }
    const size_t n = std::strlen(s);
    char *out = static_cast<char *>(std::malloc(n + 1));
    if (!out) {
        return nullptr;
    }
    std::memcpy(out, s, n + 1);
    return out;
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
    return dupCString("fake-path-transcript");
}

KEA_FAKE_EXPORT char *parakeet_capi_transcribe_pcm(parakeet_ctx * /*ctx*/, const float * /*samples*/,
                                                    int nSamples, int /*sampleRate*/,
                                                    int /*decoder*/)
{
    if (nSamples <= 0) {
        return dupCString("");
    }
    return dupCString("fake-offline-transcript");
}

KEA_FAKE_EXPORT void parakeet_capi_free_string(char *s)
{
    std::free(s);
}

KEA_FAKE_EXPORT const char *parakeet_capi_last_error(parakeet_ctx *ctx)
{
    return ctx ? ctx->lastError.c_str() : "";
}

// --- Streaming (optional; offline when begin returns null) ---

KEA_FAKE_EXPORT parakeet_stream *parakeet_capi_stream_begin(parakeet_ctx *ctx)
{
    if (!ctx || !streamingEnabled()) {
        if (ctx) {
            ctx->lastError = "not a streaming model";
        }
        return nullptr;
    }
    auto *s = new parakeet_stream;
    s->ctx = ctx;
    return s;
}

KEA_FAKE_EXPORT parakeet_stream *parakeet_capi_stream_begin_lang(parakeet_ctx *ctx,
                                                                  const char * /*lang*/)
{
    return parakeet_capi_stream_begin(ctx);
}

KEA_FAKE_EXPORT char *parakeet_capi_stream_feed(parakeet_stream *stream, const float * /*pcm*/,
                                                 int nSamples, int *eouOut)
{
    if (eouOut) {
        *eouOut = 0;
    }
    if (!stream || nSamples <= 0) {
        return nullptr;
    }
    ++stream->feedCount;
    // Emit text on first feed so worker tests can observe textFinalized.
    if (stream->feedCount == 1) {
        return dupCString("fake-stream-partial");
    }
    return dupCString("");
}

KEA_FAKE_EXPORT char *parakeet_capi_stream_finalize(parakeet_stream *stream)
{
    if (!stream) {
        return nullptr;
    }
    return dupCString("fake-stream-final");
}

KEA_FAKE_EXPORT void parakeet_capi_stream_free(parakeet_stream *stream)
{
    delete stream;
}
