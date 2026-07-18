/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * TextTransformer — post-process a finalized ASR utterance before commit.
 *
 * Styles are prompt presets (Correct is the default when post-processing is
 * enabled). Implementations may be NoOp (verbatim), Fake (tests), or
 * LlamaCpp (on-device GGUF via llama.cpp).
 *
 * All heavy work must stay off the GUI and ASR worker threads (issue #6).
 */
#pragma once

#include <QObject>
#include <QString>

namespace kea {

/// User-facing rewrite presets. Order matches the Settings ComboBox.
enum class TransformStyle {
    Correct = 0,      ///< Fix grammar/spelling; keep meaning (default when enabled)
    Enhance = 1,      ///< Clearer phrasing; drop fillers
    Professional = 2, ///< More formal / business tone
    Casual = 3,       ///< More conversational
};

class TextTransformer : public QObject
{
    Q_OBJECT

public:
    explicit TextTransformer(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    /// Human-readable id for logs/UI.
    virtual QString backendName() const = 0;

    /// True when a model is loaded and transform() can run (NoOp always true).
    virtual bool isReady() const = 0;

    /// Load (or reload) a GGUF. Empty path unloads. Returns false on failure.
    virtual bool loadModel(const QString &ggufPath) = 0;

    virtual void unloadModel() = 0;

    /// Synchronous transform. Empty input → empty output. On failure return
    /// the original text and set lastError().
    virtual QString transform(const QString &utterance, TransformStyle style) = 0;

    QString lastError() const { return m_lastError; }

    static QString styleName(TransformStyle s);
    static QString stylePrompt(TransformStyle s);
    static int styleCount() { return 4; }
    static TransformStyle styleFromInt(int v);

protected:
    void setError(const QString &e) { m_lastError = e; }

    QString m_lastError;
};

/// Passthrough — used when post-processing is disabled.
class NoOpTransformer : public TextTransformer
{
    Q_OBJECT
public:
    explicit NoOpTransformer(QObject *parent = nullptr)
        : TextTransformer(parent)
    {
    }

    QString backendName() const override { return QStringLiteral("noop"); }
    bool isReady() const override { return true; }
    bool loadModel(const QString &) override { return true; }
    void unloadModel() override {}
    QString transform(const QString &utterance, TransformStyle) override
    {
        m_lastError.clear();
        return utterance;
    }
};

/// Deterministic test double (optional prefix, records last call).
class FakeTransformer : public TextTransformer
{
    Q_OBJECT
public:
    explicit FakeTransformer(QObject *parent = nullptr)
        : TextTransformer(parent)
    {
    }

    QString prefix = QStringLiteral("[fixed] ");
    bool failNext = false;
    QString lastInput;
    TransformStyle lastStyle = TransformStyle::Correct;
    int callCount = 0;
    bool modelLoaded = false;

    QString backendName() const override { return QStringLiteral("fake"); }
    bool isReady() const override { return modelLoaded; }
    bool loadModel(const QString &path) override
    {
        modelLoaded = !path.isEmpty();
        return modelLoaded;
    }
    void unloadModel() override { modelLoaded = false; }
    QString transform(const QString &utterance, TransformStyle style) override
    {
        ++callCount;
        lastInput = utterance;
        lastStyle = style;
        if (failNext) {
            failNext = false;
            setError(QStringLiteral("fake transform failed"));
            return utterance;
        }
        m_lastError.clear();
        return prefix + utterance;
    }
};

} // namespace kea
