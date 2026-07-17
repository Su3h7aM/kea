/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "readiness.h"

#include <QWaylandClientExtension>

#include "app_settings.h"
#include "controller/dictation_controller.h"
#include "insert/input_method.h"
#include "insert/text_committer.h"

namespace kea {

Readiness::Readiness(AppSettings *settings,
                     InputMethod *inputMethod,
                     TextCommitter *committer,
                     DictationController *dictation,
                     QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_inputMethod(inputMethod)
    , m_committer(committer)
    , m_dictation(dictation)
{
    if (m_settings) {
        connect(m_settings, &AppSettings::modelPathChanged, this, &Readiness::refresh);
    }
    if (m_inputMethod) {
        connect(m_inputMethod, &QWaylandClientExtension::activeChanged, this, &Readiness::refresh);
        connect(m_inputMethod, &InputMethod::contextChanged, this, &Readiness::refresh);
    }
    if (m_committer) {
        connect(m_committer, &TextCommitter::canCommitChanged, this, &Readiness::refresh);
    }
    if (m_dictation) {
        connect(m_dictation, &DictationController::modelLoadedChanged, this, &Readiness::refresh);
        connect(m_dictation, &DictationController::lastErrorChanged, this, &Readiness::refresh);
    }
}

bool Readiness::modelReady() const
{
    return m_settings && m_settings->modelFileExists();
}

bool Readiness::inputMethodBound() const
{
    return m_inputMethod && m_inputMethod->isActive();
}

bool Readiness::textFieldActive() const
{
    return m_committer && m_committer->canCommit();
}

bool Readiness::readyToDictate() const
{
    return modelReady() && inputMethodBound();
}

QString Readiness::summary() const
{
    if (!modelReady()) {
        return QStringLiteral("Download or select a streaming GGUF model to get started.");
    }
    if (!inputMethodBound()) {
        return QStringLiteral("Input method not bound — run on Plasma Wayland and free the IME slot.");
    }
    if (!textFieldActive()) {
        return QStringLiteral("Ready — focus a text field, then hold the hotkey to dictate.");
    }
    return QStringLiteral("Ready — text field focused. Hold the hotkey and speak.");
}

QString Readiness::modelHint() const
{
    if (modelReady()) {
        return m_settings->modelPath();
    }
    return QStringLiteral("Missing: %1").arg(m_settings ? m_settings->modelPath() : QString());
}

QString Readiness::inputMethodHint() const
{
    if (inputMethodBound()) {
        return textFieldActive()
            ? QStringLiteral("Active on the focused text field")
            : QStringLiteral("Bound — focus any text field");
    }
    return QStringLiteral("Not bound. Disable fcitx5/IBus, or ensure you're on Wayland.");
}

void Readiness::refresh()
{
    Q_EMIT changed();
}

} // namespace kea
