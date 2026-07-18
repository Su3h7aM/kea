/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * InsertionRouter — deliver finalized text into the focused client.
 *
 * Ordered insert chain (stop at first success):
 *
 *   1. Input method (KWin input_method_unstable_v1 → commit_string)
 *      Prefer this: true caret insert when the client enables text-input.
 *   2. (Future) optional inject backends — virtual key / portal / helper tool.
 *      Slot in *before* clipboard when implemented. Not client-toolkit IMs
 *      (GTK/Qt IM modules live inside the target app; Kea cannot implement those).
 *   3. Clipboard — always the *last* fallback: recover text for paste when
 *      nothing else can insert. Never preferred over a real insert path.
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
    /// Which step of the chain delivered the text (or None if nothing did).
    enum class Path {
        None = 0,
        InputMethod = 1,
        /// Reserved for a future mid-chain inject backend (before clipboard).
        Inject = 2,
        Clipboard = 3, ///< last resort only
    };
    Q_ENUM(Path)

    struct Result {
        bool delivered = false; ///< true if any chain step accepted the text
        Path path = Path::None;
        QString detail; ///< error or human status (empty on pure IM success)
    };

    explicit InsertionRouter(QObject *parent = nullptr);

    void setTextCommitter(TextCommitter *committer);
    TextCommitter *textCommitter() const { return m_committer; }

    /// When true (default), step 3 (clipboard) runs if earlier steps fail.
    bool clipboardFallbackEnabled() const { return m_clipboardFallback; }
    void setClipboardFallbackEnabled(bool enabled);

    bool canUseInputMethod() const;

    /// Run the insert chain: IM → (future inject) → clipboard (if enabled).
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
    /// Step 1.
    bool tryInputMethod(const QString &text, QString *errorOut);
    /// Step 3 (last).
    bool tryClipboardLastResort(const QString &text, QString *errorOut);

    TextCommitter *m_committer = nullptr;
    bool m_clipboardFallback = true;
    QString m_lastDetail;
    Path m_lastPath = Path::None;
};

} // namespace kea
