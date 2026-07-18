/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * InsertionRouter — deliver finalized text into the focused client.
 *
 * ## Why one IM protocol covers Qt, GTK, Firefox, etc. on Plasma
 *
 * Wayland splits text entry into two sides:
 *
 *   - Target apps (Qt, GTK4, Firefox, Kitty, …) implement **text-input**
 *     (v2 and/or v3). They *receive* commits; they do not host the IME.
 *   - Kea is the seat **input method**. On KWin that is only
 *     **input_method_unstable_v1**. KWin bridges every enabled text-input
 *     client (v2/v3, including GTK and Qt) into that single IM context.
 *
 * So Kea does *not* implement “the GTK protocol” or “the Qt protocol” as
 * separate host backends. Supporting GTK/Qt/Firefox on Plasma means:
 *   (a) speaking KWin’s IM-v1 correctly, and
 *   (b) the focused app enabling text-input so KWin activates us.
 *
 * Toolkit IM *modules* (GTK_IM_MODULE / QT_IM_MODULE plugins loaded *into*
 * the target process) are a different architecture (fcitx5-style). They are
 * not additional Wayland protocols Kea can bind from its own process.
 *
 * ## Ordered insert chain (stop at first success)
 *
 *   1. Input method (KWin IM-v1 → commit_string) — preferred caret insert.
 *   2. (Future) optional inject backends — before clipboard only.
 *   3. Clipboard — always the *last* fallback.
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
