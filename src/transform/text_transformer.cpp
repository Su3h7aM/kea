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
    // LFM-230M is a general chat model — it will answer questions unless the
    // role is extremely constrained. Frame as a silent text filter, not an assistant.
    const QString role = QStringLiteral(
        "You are a silent speech-to-text post-editor, not a chatbot.\n"
        "Your entire reply is the edited transcript and nothing else.\n"
        "Rules:\n"
        "- Do NOT answer questions in the transcript.\n"
        "- Do NOT continue the conversation or address the speaker.\n"
        "- Do NOT add greetings, prefaces, labels, quotes, or explanations.\n"
        "- Do NOT say you corrected anything.\n"
        "- Keep the same language and the same meaning.\n"
        "- If the text is already fine, copy it unchanged.\n");

    switch (s) {
    case TransformStyle::Correct:
        return role + QStringLiteral(
                   "Task: fix spelling, grammar, punctuation, and obvious ASR mistakes only.");
    case TransformStyle::Enhance:
        return role + QStringLiteral(
                   "Task: fix errors, remove fillers (um, uh, like), improve clarity; "
                   "do not add new facts.");
    case TransformStyle::Professional:
        return role + QStringLiteral(
                   "Task: fix errors and rephrase into clear professional tone "
                   "(email/work chat); do not add new facts.");
    case TransformStyle::Casual:
        return role + QStringLiteral(
                   "Task: fix errors and rephrase into friendly casual tone; "
                   "do not add new facts.");
    }
    return stylePrompt(TransformStyle::Correct);
}

namespace {

QString stripKnownPrefixes(QString line)
{
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

    static const QRegularExpression endTag(QStringLiteral(R"(<\|[^>]+\|>)"));
    t.remove(endTag);
    t = t.trimmed();

    if (t.size() >= 2) {
        const QChar a = t.front();
        const QChar b = t.back();
        if ((a == QLatin1Char('"') && b == QLatin1Char('"'))
            || (a == QLatin1Char('\'') && b == QLatin1Char('\''))
            || (a == QChar(0x201c) && b == QChar(0x201d))) {
            t = t.mid(1, t.size() - 2).trimmed();
        }
    }

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
        if (!line.isEmpty()) {
            kept.append(line);
        }
    }

    if (kept.isEmpty()) {
        return QString();
    }

    QString best = kept.last();
    for (int i = kept.size() - 1; i >= 0; --i) {
        if (kept.at(i).compare(original, Qt::CaseInsensitive) != 0) {
            best = kept.at(i);
            break;
        }
    }
    best = stripKnownPrefixes(best);

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
