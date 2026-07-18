/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "model_verify.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace kea {

namespace {

bool startsWithGgufMagic(QFile &f)
{
    if (!f.seek(0)) {
        return false;
    }
    const QByteArray head = f.read(4);
    return head == QByteArrayLiteral("GGUF");
}

QString sha256HexOfFile(QFile &f)
{
    if (!f.seek(0)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr qint64 kChunk = 1 << 20; // 1 MiB
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(kChunk);
        if (chunk.isEmpty()) {
            break;
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

ModelIntegrityResult verifyModelFile(const QString &path,
                                     const ModelIntegrityExpect &expect)
{
    ModelIntegrityResult r;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        r.error = QStringLiteral("downloaded file is missing: %1").arg(path);
        return r;
    }

    const qint64 size = fi.size();
    if (size < kGgufMinBytes) {
        r.error = QStringLiteral(
                      "downloaded file is too small to be a GGUF model (%1 bytes)")
                      .arg(size);
        return r;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("cannot open downloaded file for verification: %1")
                      .arg(path);
        return r;
    }

    if (!startsWithGgufMagic(f)) {
        r.error = QStringLiteral(
            "downloaded file is not a GGUF model (missing GGUF magic header)");
        return r;
    }

    if (expect.sizeBytes > 0 && size != expect.sizeBytes) {
        r.error = QStringLiteral(
                      "downloaded size %1 does not match catalog size %2")
                      .arg(size)
                      .arg(expect.sizeBytes);
        return r;
    }

    if (!expect.sha256.isEmpty()) {
        const QString actual = sha256HexOfFile(f);
        if (actual.isEmpty()) {
            r.error = QStringLiteral("failed to hash downloaded file");
            return r;
        }
        if (actual.compare(expect.sha256, Qt::CaseInsensitive) != 0) {
            r.error = QStringLiteral(
                          "SHA-256 mismatch (got %1, expected %2)")
                          .arg(actual, expect.sha256.toLower());
            return r;
        }
    }

    r.ok = true;
    return r;
}

} // namespace kea
