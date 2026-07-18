/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * InsertionRouter — deliver finalized text into the focused client.
 *
 * Primary: Wayland input-method-v1 via TextCommitter (caret insert).
 * Fallback: clipboard copy when no IM context (terminals, GTK surfaces that
 * never enable text-input, etc.). Auto key-inject (ydotool/portal) is out of
 * scope for this layer.
 */
#pragma once

#include <QObject>
#include <QString>

namespace kea {

class TextCommitter;

class InsertionRouter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool clipboardFallbackEnabled READ clipboardFallbackEnabled
                   WRITE setClipboardFallbackEnabled NOTIFY clipboardFallbackEnabledChanged)
    Q_PROPERTY(bool canUseInputMethod READ canUseInputMethod NOTIFY canUseInputMethodChanged)

public:
    enum class Path {
        None = 0,
        InputMethod = 1,
        Clipboard = 2,
    };
    Q_ENUM(Path)

    struct Result {
        bool delivered = false; ///< true if IM commit succeeded or clipboard got the text
        Path path = Path::None;
        QString detail; ///< error or human status (empty on pure IM success)
    };

    explicit InsertionRouter(QObject *parent = nullptr);

    void setTextCommitter(TextCommitter *committer);
    TextCommitter *textCommitter() const { return m_committer; }

    bool clipboardFallbackEnabled() const { return m_clipboardFallback; }
    void setClipboardFallbackEnabled(bool enabled);

    bool canUseInputMethod() const;

    /// Try IM commit; on failure optionally copy to clipboard.
    Result insertText(const QString &text);

    bool setPreedit(const QString &text);
    bool clearPreedit();

    QString lastDetail() const { return m_lastDetail; }
    Path lastPath() const { return m_lastPath; }

Q_SIGNALS:
    void clipboardFallbackEnabledChanged();
    void canUseInputMethodChanged(bool can);
    void inserted(Path path, const QString &text);

private:
    bool copyToClipboard(const QString &text, QString *errorOut);

    TextCommitter *m_committer = nullptr;
    bool m_clipboardFallback = true;
    QString m_lastDetail;
    Path m_lastPath = Path::None;
};

} // namespace kea
