/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "app_settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include "hotkey/global_hotkey.h"

namespace kea {

QString AppSettings::defaultModelPath()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/kea/models");
    return base + QStringLiteral("/parakeet_realtime_eou_120m-q8_0.gguf");
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_modelPath(defaultModelPath())
    , m_hotkey(GlobalHotkey::defaultSequence())
{
    load();
}

void AppSettings::load()
{
    QSettings s(QStringLiteral("kea"), QStringLiteral("kea"));
    m_modelPath = s.value(QStringLiteral("modelPath"), defaultModelPath()).toString();
    m_backend = s.value(QStringLiteral("backend"), 0).toInt();
    const QString hot = s.value(QStringLiteral("hotkey"),
                                GlobalHotkey::defaultSequence().toString(QKeySequence::PortableText))
                            .toString();
    m_hotkey = QKeySequence::fromString(hot, QKeySequence::PortableText);
    if (m_hotkey.isEmpty()) {
        m_hotkey = GlobalHotkey::defaultSequence();
    }
}

void AppSettings::save() const
{
    QSettings s(QStringLiteral("kea"), QStringLiteral("kea"));
    s.setValue(QStringLiteral("modelPath"), m_modelPath);
    s.setValue(QStringLiteral("backend"), m_backend);
    s.setValue(QStringLiteral("hotkey"), m_hotkey.toString(QKeySequence::PortableText));
}

void AppSettings::setModelPath(const QString &path)
{
    if (path == m_modelPath) {
        return;
    }
    m_modelPath = path;
    save();
    Q_EMIT modelPathChanged();
}

void AppSettings::setBackend(int backend)
{
    backend = (backend == 1) ? 1 : 0;
    if (backend == m_backend) {
        return;
    }
    m_backend = backend;
    save();
    Q_EMIT backendChanged();
}

void AppSettings::setHotkey(const QKeySequence &seq)
{
    if (seq == m_hotkey) {
        return;
    }
    m_hotkey = seq.isEmpty() ? GlobalHotkey::defaultSequence() : seq;
    save();
    Q_EMIT hotkeyChanged();
}

void AppSettings::setHotkeyString(const QString &s)
{
    setHotkey(QKeySequence::fromString(s, QKeySequence::NativeText));
}

} // namespace kea
