/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "parakeet_backend.h"

#include <dlfcn.h>

#include <QCoreApplication>
#include <QDir>

// parakeet_capi.h is the upstream C-API header. We deliberately do NOT include
// it here: that header only exists after the build-time fetch of parakeet.cpp
// (see cmake/parakeet.cmake), and the dlopen loader needs to compile even
// before that fetch has run. Instead we declare the single opaque type the
// offline path needs. The streaming types (parakeet_stream, ...) will be added
// in Phase 1 alongside the streaming symbols.
extern "C" {
typedef struct parakeet_ctx parakeet_ctx;
}

namespace kea {

// Mirrors the C-API symbols Kea depends on (offline path for Phase 0).
struct ParakeetBackend::Symbols {
    int (*capi_abi_version)();
    parakeet_ctx *(*capi_load)(const char *);
    void (*capi_free)(parakeet_ctx *);
    char *(*capi_transcribe_path)(parakeet_ctx *, const char *, int);
    void (*capi_free_string)(char *);
    const char *(*capi_last_error)(parakeet_ctx *);
};

// Thin owner so the header doesn't expose parakeet_ctx to QML-facing code.
class ParakeetCtxHandle
{
public:
    parakeet_ctx *ctx = nullptr;
};

ParakeetBackend::ParakeetBackend()
    : m_ctx(new ParakeetCtxHandle)
    , m_sym(std::make_unique<Symbols>())
{
}

ParakeetBackend::~ParakeetBackend()
{
    if (m_sym && m_sym->capi_free && m_ctx->ctx) {
        m_sym->capi_free(m_ctx->ctx);
    }
    if (m_handle) {
        dlclose(m_handle);
    }
}

QString ParakeetBackend::libraryPath(ParakeetDevice device)
{
    // Paths baked in by CMake (parakeet.cmake). Vulkan path is empty when
    // the Vulkan variant was skipped at configure time.
    switch (device) {
    case ParakeetDevice::Cpu:
        return QStringLiteral(KEA_PARAKEET_CPU_LIB);
    case ParakeetDevice::Vulkan:
        return QStringLiteral(KEA_PARAKEET_VULKAN_LIB);
    }
    return {};
}

bool ParakeetBackend::load(ParakeetDevice device)
{
    const QString path = libraryPath(device);
    if (path.isEmpty()) {
        setError(QStringLiteral("backend variant not built (path empty)"));
        return false;
    }
    if (!QFile::exists(path)) {
        setError(QStringLiteral("backend library not found at %1 (run the build / parakeet_all target)").arg(path));
        return false;
    }

    m_handle = dlopen(path.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle) {
        setError(QStringLiteral("dlopen(%1) failed: %2").arg(path, QString::fromUtf8(dlerror())));
        return false;
    }

    dlerror(); // clear
    m_sym->capi_abi_version = reinterpret_cast<int (*)()>(dlsym(m_handle, "parakeet_capi_abi_version"));
    m_sym->capi_load = reinterpret_cast<parakeet_ctx *(*)(const char *)>(dlsym(m_handle, "parakeet_capi_load"));
    m_sym->capi_free = reinterpret_cast<void (*)(parakeet_ctx *)>(dlsym(m_handle, "parakeet_capi_free"));
    m_sym->capi_transcribe_path = reinterpret_cast<char *(*)(parakeet_ctx *, const char *, int)>(dlsym(m_handle, "parakeet_capi_transcribe_path"));
    m_sym->capi_free_string = reinterpret_cast<void (*)(char *)>(dlsym(m_handle, "parakeet_capi_free_string"));
    m_sym->capi_last_error = reinterpret_cast<const char *(*)(parakeet_ctx *)>(dlsym(m_handle, "parakeet_capi_last_error"));

    if (!m_sym->capi_abi_version || !m_sym->capi_load || !m_sym->capi_free
        || !m_sym->capi_transcribe_path || !m_sym->capi_free_string || !m_sym->capi_last_error) {
        setError(QStringLiteral("missing parakeet C-API symbols in %1").arg(path));
        dlclose(m_handle);
        m_handle = nullptr;
        return false;
    }
    return true;
}

bool ParakeetBackend::loadModel(const QString &ggufPath)
{
    if (!isLoaded()) {
        setError(QStringLiteral("backend not loaded"));
        return false;
    }
    if (m_ctx->ctx) {
        m_sym->capi_free(m_ctx->ctx);
        m_ctx->ctx = nullptr;
    }
    m_ctx->ctx = m_sym->capi_load(ggufPath.toUtf8().constData());
    if (!m_ctx->ctx) {
        setError(QStringLiteral("parakeet_capi_load failed for %1: %2")
                     .arg(ggufPath, QString::fromUtf8(m_sym->capi_last_error(nullptr))));
        return false;
    }
    return true;
}

TranscriptionResult ParakeetBackend::transcribePath(const QString &wavPath)
{
    TranscriptionResult r;
    if (!hasModel()) {
        r.text = QStringLiteral("no model loaded");
        return r;
    }
    char *text = m_sym->capi_transcribe_path(m_ctx->ctx, wavPath.toUtf8().constData(), 0 /*default decoder*/);
    if (!text) {
        r.text = QStringLiteral("transcribe failed: %1")
                     .arg(QString::fromUtf8(m_sym->capi_last_error(m_ctx->ctx)));
        return r;
    }
    r.ok = true;
    r.text = QString::fromUtf8(text);
    m_sym->capi_free_string(text);
    return r;
}

void ParakeetBackend::setError(const QString &msg)
{
    m_lastError = msg;
}

} // namespace kea
