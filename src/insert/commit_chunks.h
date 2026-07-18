/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Split text for Wayland commit_string.
 *
 * fcitx5's waylandim frontend caps each commit under ~4000 UTF-8 bytes so a
 * single request stays under the compositor's 4096-byte wl_message limit
 * (documented for zwp_input_method_v2; the same limit applies in practice on
 * v1). Split only on Unicode character boundaries.
 */
#pragma once

#include <QList>
#include <QString>

namespace kea {

/// Max UTF-8 bytes per commit_string (fcitx5 uses 4000).
inline constexpr int kWaylandCommitUtf8Limit = 4000;

/// Split `text` into chunks each encoding to at most `maxUtf8Bytes` bytes.
/// Empty input → empty list. Invalid UTF-16 unpaired surrogates are replaced
/// by QString::toUtf8's usual handling.
QList<QString> splitForWaylandCommit(const QString &text,
                                     int maxUtf8Bytes = kWaylandCommitUtf8Limit);

} // namespace kea
