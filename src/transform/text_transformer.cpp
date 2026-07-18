/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "text_transformer.h"

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
    // System-side instructions for LFM / instruct models. Keep them short:
    // 230M-class models follow tight, explicit rules better than long prose.
    switch (s) {
    case TransformStyle::Correct:
        return QStringLiteral(
            "You fix speech-to-text transcripts. Correct spelling, grammar, "
            "punctuation, and obvious ASR mistakes. Keep the same language and "
            "meaning. Do not add content. Output only the corrected transcript.");
    case TransformStyle::Enhance:
        return QStringLiteral(
            "You polish speech-to-text transcripts. Fix errors, remove fillers "
            "(um, uh, like), and improve clarity while preserving meaning and "
            "language. Output only the polished transcript.");
    case TransformStyle::Professional:
        return QStringLiteral(
            "You rewrite speech-to-text transcripts in a clear professional "
            "tone suitable for email or work chat. Fix errors, keep meaning, "
            "same language. Output only the rewritten text.");
    case TransformStyle::Casual:
        return QStringLiteral(
            "You rewrite speech-to-text transcripts in a friendly casual tone. "
            "Fix errors, keep meaning, same language. Output only the rewritten text.");
    }
    return stylePrompt(TransformStyle::Correct);
}

} // namespace kea
