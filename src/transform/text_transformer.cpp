/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "text_transformer.h"

#include <QRegularExpression>
#include <QStringList>

namespace kea {

QString TextTransformer::styleName(TransformStyle s)
{
    switch (s) {
    case TransformStyle::Correct:
        return QStringLiteral("Correct");
    case TransformStyle::Enhance:
        return QStringLiteral("Enhance");
    case TransformStyle::Professional:
        return QStringLiteral("Professional");
    case TransformStyle::Casual:
        return QStringLiteral("Casual");
    }
    return QStringLiteral("Correct");
}

TransformStyle TextTransformer::styleFromInt(int v)
{
    if (v < 0 || v > 3) {
        return TransformStyle::Correct;
    }
    return static_cast<TransformStyle>(v);
}

QString TextTransformer::stylePrompt(TransformStyle s)
{
    // Tiny instruct models (LFM 230M) ignore long policies; keep rules absolute
    // and ban explanations. The user turn repeats "reply with only the text".
    const QString common = QStringLiteral(
        "You rewrite speech-to-text. Reply with ONLY the final text — nothing else. "
        "Do not write labels (no Corrected/Original/Rewritten). "
        "Do not explain. Do not quote the whole answer. "
        "Do not repeat the input before the answer. "
        "Same language and meaning as the input. ");

    switch (s) {
    case TransformStyle::Correct:
        return common + QStringLiteral(
                   "Fix spelling, grammar, punctuation, and obvious ASR errors only.");
    case TransformStyle::Enhance:
        return common + QStringLiteral(
                   "Fix errors, drop fillers (um, uh, like), make it clearer and concise.");
    case TransformStyle::Professional:
        return common + QStringLiteral(
                   "Fix errors and use a clear professional tone (email / work chat).");
    case TransformStyle::Casual:
        return common + QStringLiteral(
                   "Fix errors and use a friendly casual tone.");
    }
    return stylePrompt(TransformStyle::Correct);
}

namespace {

QString stripKnownPrefixes(QString line)
{
    // Repeatedly strip common small-model preambles from the start of a line.
    static const QRegularExpression prefix(QStringLiteral(
        "^\\s*(?:"
        "here(?:'s| is)\\s+(?:the\\s+)?(?:corrected|rewritten|polished|fixed|improved)\\s+"
        "(?:version|transcript|text)?\\s*[:\\-–—]?\\s*"
        "|"
        "(?:corrected|correction|rewritten|rewrite|fixed|output|result|polished|enhanced|"
        "professional|final|answer|improved)\\s*(?:version|transcript|text)?\\s*[:\\-–—]\\s*"
        "|"
        "(?:original|input|transcript|raw|source|asr)\\s*[:\\-–—]\\s*"
        ")"),
        QRegularExpression::CaseInsensitiveOption);

    for (int i = 0; i < 4; ++i) {
        const auto m = prefix.match(line);
        if (!m.hasMatch() || m.capturedLength(0) <= 0) {
            break;
        }
        line = line.mid(m.capturedLength(0)).trimmed();
    }
    return line;
}

/// Only drop lines that are *labeled* as the original input — not lines that
/// simply equal the ASR text (a valid no-op correction is "same as input").
bool isLabeledOriginalLine(const QString &line, const QString &original)
{
    static const QRegularExpression origLabel(QStringLiteral(
        "^\\s*(?:original|input|transcript|raw|source|asr)\\s*[:\\-–—]\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = origLabel.match(line);
    if (!m.hasMatch()) {
        return false;
    }
    QString body = line.mid(m.capturedLength(0)).trimmed();
    return body.isEmpty() || body.compare(original, Qt::CaseInsensitive) == 0;
}

} // namespace

QString TextTransformer::sanitizeTransformOutput(const QString &raw, const QString &original)
{
    QString t = raw.trimmed();
    if (t.isEmpty()) {
        return t;
    }

    // Drop chat/end markers the model sometimes echoes.
    static const QRegularExpression endTag(QStringLiteral(R"(<\|[^>]+\|>)"));
    t.remove(endTag);
    t = t.trimmed();

    // Unwrap a single pair of surrounding quotes.
    if (t.size() >= 2) {
        const QChar a = t.front();
        const QChar b = t.back();
        if ((a == QLatin1Char('"') && b == QLatin1Char('"'))
            || (a == QLatin1Char('\'') && b == QLatin1Char('\''))
            || (a == QChar(0x201c) && b == QChar(0x201d))) {
            t = t.mid(1, t.size() - 2).trimmed();
        }
    }

    // Line-wise: drop "Original: …" echoes; strip labels; keep real content.
    const QStringList lines = t.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QStringList kept;
    kept.reserve(lines.size());
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (isLabeledOriginalLine(line, original)) {
            continue;
        }
        line = stripKnownPrefixes(line);
        if (line.isEmpty()) {
            continue;
        }
        // Skip if after stripping we only have the original again.
        if (line.compare(original, Qt::CaseInsensitive) == 0 && lines.size() > 1) {
            // Keep for now — we may fall back; prefer non-echo lines later.
        }
        kept.append(line);
    }

    if (kept.isEmpty()) {
        return QString();
    }

    // Prefer the last line that is not a copy of the original
    // (models often print original then rewrite).
    QString best = kept.last();
    for (int i = kept.size() - 1; i >= 0; --i) {
        if (kept.at(i).compare(original, Qt::CaseInsensitive) != 0) {
            best = kept.at(i);
            break;
        }
    }
    best = stripKnownPrefixes(best);

    // "original text. rewritten text" without newlines: drop a leading copy.
    const QString orig = original.trimmed();
    if (!orig.isEmpty() && best.size() > orig.size() + 1
        && best.startsWith(orig, Qt::CaseInsensitive)) {
        QString rest = best.mid(orig.size()).trimmed();
        while (!rest.isEmpty()) {
            const QChar c = rest.front();
            if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char(':')
                || c == QLatin1Char('|') || c == QLatin1Char('/') || c == QLatin1Char('.')
                || c == QChar(0x2013) || c == QChar(0x2014)) {
                rest = rest.mid(1).trimmed();
                continue;
            }
            break;
        }
        if (!rest.isEmpty()) {
            best = stripKnownPrefixes(rest);
        }
    }

    return best.trimmed();
}

} // namespace kea
