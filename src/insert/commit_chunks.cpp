/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "commit_chunks.h"

#include <QByteArray>

namespace kea {

QList<QString> splitForWaylandCommit(const QString &text, int maxUtf8Bytes)
{
    QList<QString> out;
    if (text.isEmpty() || maxUtf8Bytes <= 0) {
        return out;
    }

    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() <= maxUtf8Bytes) {
        out.append(text);
        return out;
    }

    // Walk UTF-8 by code point so we never split a multi-byte character.
    int start = 0;
    const int n = utf8.size();
    while (start < n) {
        int end = start;
        while (end < n) {
            const unsigned char c = static_cast<unsigned char>(utf8.at(end));
            int charLen = 1;
            if ((c & 0x80) == 0) {
                charLen = 1;
            } else if ((c & 0xE0) == 0xC0) {
                charLen = 2;
            } else if ((c & 0xF0) == 0xE0) {
                charLen = 3;
            } else if ((c & 0xF8) == 0xF0) {
                charLen = 4;
            } else {
                // Invalid lead — skip one byte to avoid infinite loop.
                charLen = 1;
            }
            if (end + charLen > n) {
                charLen = n - end;
            }
            if (end + charLen - start > maxUtf8Bytes) {
                break;
            }
            end += charLen;
        }
        if (end == start) {
            // Single code point larger than limit (shouldn't happen for UTF-8
            // max 4 bytes) — force progress.
            end = qMin(start + 1, n);
        }
        out.append(QString::fromUtf8(utf8.constData() + start, end - start));
        start = end;
    }
    return out;
}

} // namespace kea
