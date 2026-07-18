/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * InsertionRouter — deliver finalized text into the focused client.
 *
 * Fixed order (always; not user-configurable):
 *
 *   1. Input method (KWin input_method_unstable_v1 → commit_string)
 *   2. fake_input keysyms (org_kde_kwin_fake_input) when IM has no context
 *   3. Clipboard last resort (always attempted if 1–2 fail)
 *
 * Target apps use text-input v2/v3; KWin bridges them to IM-v1. Toolkit IM
 * modules are not implemented in-process.
 */
#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace kea {

class FakeInputClient;
class TextCommitter;

class InsertionRouter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUseInputMethod READ canUseInputMethod NOTIFY canUseInputMethodChanged)

public:
    enum class Path {
        None = 0,
        InputMethod = 1,
        FakeInput = 2,
        Clipboard = 3,
    };
    Q_ENUM(Path)

    struct Result {
        bool delivered = false;
        Path path = Path::None;
        QString detail;
    };

    explicit InsertionRouter(QObject *parent = nullptr);
    ~InsertionRouter() override;

    void setTextCommitter(TextCommitter *committer);
    TextCommitter *textCommitter() const { return m_committer; }

    bool canUseInputMethod() const;

    /// Run IM → fake_input → clipboard until one succeeds.
    Result insertText(const QString &text);

    bool setPreedit(const QString &text);
    bool clearPreedit();

    QString lastDetail() const { return m_lastDetail; }
    Path lastPath() const { return m_lastPath; }

Q_SIGNALS:
    void canUseInputMethodChanged(bool can);
    void inserted(Path path, const QString &text);

private:
    bool tryInputMethod(const QString &text, QString *errorOut);
    bool tryFakeInput(const QString &text, QString *errorOut);
    bool tryClipboardLastResort(const QString &text, QString *errorOut);

    TextCommitter *m_committer = nullptr;
    std::unique_ptr<FakeInputClient> m_fakeInput;
    QString m_lastDetail;
    Path m_lastPath = Path::None;
};

} // namespace kea
