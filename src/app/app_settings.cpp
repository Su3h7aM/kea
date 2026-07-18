/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "app_settings.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include "hotkey/global_hotkey.h"
#include "logging.h"

namespace kea {

QString AppSettings::defaultModelsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/kea/models");
}

QString AppSettings::defaultModelPath()
{
    // KEA_MODEL overrides the default absolute path (no hard-coded machine paths).
    const QByteArray env = qgetenv("KEA_MODEL");
    if (!env.isEmpty()) {
        return QString::fromUtf8(env);
    }
    // Default: XDG data dir + offline TDT 0.6B v3 filename.
    return defaultModelsDir() + QStringLiteral("/tdt-0.6b-v3-q8_0.gguf");
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
    // Always use the kea/kea config (independent of QCoreApplication names so
    // tests cannot pollute the user config via organizationName overrides).
    QSettings s(QStringLiteral("kea"), QStringLiteral("kea"));
    m_modelPath = s.value(QStringLiteral("modelPath"), defaultModelPath()).toString();
    m_backend = s.value(QStringLiteral("backend"), 0).toInt(); // default CPU
    m_activationMode = s.value(QStringLiteral("activationMode"), 0).toInt();
    m_onboardingDone = s.value(QStringLiteral("onboardingDone"), false).toBool();
    // Missing key → factory default. Present-but-empty → user cleared (disabled).
    if (!s.contains(QStringLiteral("hotkey"))) {
        m_hotkey = GlobalHotkey::defaultSequence();
    } else {
        const QString hot = s.value(QStringLiteral("hotkey")).toString();
        m_hotkey = QKeySequence::fromString(hot, QKeySequence::PortableText);
        if (m_hotkey.isEmpty() && !hot.isEmpty()) {
            m_hotkey = QKeySequence::fromString(hot, QKeySequence::NativeText);
        }
        // Leave empty if the user explicitly cleared the shortcut.
    }
    // Never keep a path that clearly came from automated tests.
    if (m_modelPath.contains(QStringLiteral("kea-definitely-missing-model"))) {
        m_modelPath = defaultModelPath();
    }
    // Vulkan + Qt Quick on the same process has been crashy on some RADV setups;
    // keep a stored Vulkan choice, but log a hint.
    if (m_backend == 1) {
        qCInfo(keaLog) << "backend=Vulkan (if you see SIGSEGV during dictation, switch to CPU)";
    }
    qCInfo(keaLog) << "settings loaded: model=" << m_modelPath
                    << "backend=" << m_backend
                    << "onboardingDone=" << m_onboardingDone;
}

void AppSettings::save() const
{
    QSettings s(QStringLiteral("kea"), QStringLiteral("kea"));
    s.setValue(QStringLiteral("modelPath"), m_modelPath);
    s.setValue(QStringLiteral("backend"), m_backend);
    s.setValue(QStringLiteral("hotkey"), m_hotkey.toString(QKeySequence::PortableText));
    s.setValue(QStringLiteral("activationMode"), m_activationMode);
    s.setValue(QStringLiteral("onboardingDone"), m_onboardingDone);
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
    // Empty is allowed: means "no global hotkey" (tray start/stop only).
    m_hotkey = seq;
    save();
    Q_EMIT hotkeyChanged();
}

void AppSettings::setHotkeyString(const QString &s)
{
    // Accept both portable ("Ctrl+Shift+D") and native ("Ctrl+Shift+D" on Linux)
    // formats. Try portable first since that's what the config file uses and
    // what a user is most likely to type in the text field.
    QKeySequence seq = QKeySequence::fromString(s, QKeySequence::PortableText);
    if (seq.isEmpty()) {
        seq = QKeySequence::fromString(s, QKeySequence::NativeText);
    }
    setHotkey(seq);
}

void AppSettings::setActivationMode(int mode)
{
    mode = (mode == 1) ? 1 : 0;
    if (mode == m_activationMode) {
        return;
    }
    m_activationMode = mode;
    save();
    Q_EMIT activationModeChanged();
}

void AppSettings::setOnboardingDone(bool done)
{
    if (done == m_onboardingDone) {
        return;
    }
    m_onboardingDone = done;
    save();
    Q_EMIT onboardingDoneChanged();
}

bool AppSettings::modelFileExists() const
{
    return QFileInfo::exists(m_modelPath) && QFileInfo(m_modelPath).isFile();
}

QString AppSettings::modelsDir() const
{
    const QString dir = defaultModelsDir();
    QDir().mkpath(dir);
    return dir;
}

} // namespace kea
