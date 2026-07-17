/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Readiness — aggregates setup checks for the onboarding / status UI.
 *
 * Does not own resources; it just reflects model path, input-method binding,
 * and dictation error state into QML-friendly properties.
 */
#pragma once

#include <QObject>
#include <QString>

namespace kea {

class AppSettings;
class InputMethod;
class DictationController;
class TextCommitter;

class Readiness : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool modelReady READ modelReady NOTIFY changed)
    Q_PROPERTY(bool inputMethodBound READ inputMethodBound NOTIFY changed)
    Q_PROPERTY(bool textFieldActive READ textFieldActive NOTIFY changed)
    Q_PROPERTY(bool readyToDictate READ readyToDictate NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString modelHint READ modelHint NOTIFY changed)
    Q_PROPERTY(QString inputMethodHint READ inputMethodHint NOTIFY changed)

public:
    explicit Readiness(AppSettings *settings,
                       InputMethod *inputMethod,
                       TextCommitter *committer,
                       DictationController *dictation,
                       QObject *parent = nullptr);

    bool modelReady() const;
    bool inputMethodBound() const;
    bool textFieldActive() const;
    bool readyToDictate() const;
    QString summary() const;
    QString modelHint() const;
    QString inputMethodHint() const;

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void changed();

private:
    AppSettings *m_settings = nullptr;
    InputMethod *m_inputMethod = nullptr;
    TextCommitter *m_committer = nullptr;
    DictationController *m_dictation = nullptr;
};

} // namespace kea
