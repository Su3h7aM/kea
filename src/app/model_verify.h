/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Post-download integrity checks for GGUF model files.
 *
 * Always validates the GGUF magic header and a sane minimum size. Optionally
 * checks an exact byte size and/or SHA-256 when the catalog provides them.
 */
#pragma once

#include <QString>
#include <cstdint>

namespace kea {

/// GGUF magic is the four ASCII bytes "GGUF" at offset 0.
constexpr qint64 kGgufMinBytes = 1024; // anything smaller cannot be a real model

struct ModelIntegrityExpect {
    qint64 sizeBytes = 0;   ///< 0 = do not check size
    QString sha256;         ///< empty = do not check hash (hex, lower or upper)
};

/// Result of verifying a local path. `ok` false ⇒ `error` is non-empty.
struct ModelIntegrityResult {
    bool ok = false;
    QString error;
};

/// Verify `path` looks like a valid GGUF and matches optional expectations.
/// Does not rename or delete the file.
ModelIntegrityResult verifyModelFile(const QString &path,
                                     const ModelIntegrityExpect &expect = {});

} // namespace kea
