/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Kea — entry point.
 */
#include <QApplication>
#include <QIcon>
#include <QMetaType>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <QList>

#include <KAboutData>
#include <KIconTheme>
#include <KLocalizedContext>
#include <KLocalizedString>

#include "app/app_settings.h"
#include "app/model_downloader.h"
#include "app/readiness.h"
#include "app/tray_controller.h"
#include "controller/dictation_controller.h"
#include "hotkey/global_hotkey.h"
#include "insert/input_method.h"
#include "insert/text_committer.h"
#include "logging.h"

// Default offline TDT model download (must match data/models.json).
// Path resolution: $KEA_MODEL, else ~/.local/share/kea/models/<filename>
// (see AppSettings::defaultModelPath).
static const char kDefaultModelUrl[] =
    "https://huggingface.co/mudler/parakeet-cpp-gguf/resolve/main/"
    "tdt-0.6b-v3-q8_0.gguf";
static const char kDefaultModelFilename[] = "tdt-0.6b-v3-q8_0.gguf";

int main(int argc, char *argv[])
{
    qRegisterMetaType<QList<float>>("QList<float>");

    KIconTheme::initTheme();

    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kea");

    // Settings live under ~/.config/kea/kea.conf (must match AppSettings / QSettings).
    // Kea is a native-feeling KDE Plasma app but is not part of KDE itself, so the
    // app ID/domain intentionally avoid the org.kde.* namespace (see AGENTS.md).
    QApplication::setOrganizationName(QStringLiteral("kea"));
    QApplication::setApplicationName(QStringLiteral("kea"));
    QApplication::setDesktopFileName(QStringLiteral("io.github.su3h7am.kea"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    KAboutData about(QStringLiteral("kea"),
                     i18nc("@title", "Kea"),
                     QStringLiteral("0.1.0"),
                     i18nc("@info", "On-device, streaming voice dictation for KDE Plasma"),
                     KAboutLicense::MIT,
                     i18nc("@info:credit", "© 2026 Kea contributors"));
    about.addAuthor(i18nc("@info:credit", "Kea contributors"));
    about.setHomepage(QStringLiteral("https://github.com/Su3h7aM/kea"));
    about.setBugAddress("https://github.com/Su3h7aM/kea/issues");
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("audio-input-microphone")));

    qCInfo(keaLog) << "Kea" << about.version() << "starting";

    kea::AppSettings settings;
    TrayController tray;
    tray.show();

    kea::InputMethod inputMethod;
    kea::TextCommitter committer;
    kea::GlobalHotkey hotkey;
    hotkey.setSequence(settings.hotkey());

    kea::DictationController dictation(&settings);
    dictation.setTextCommitter(&committer);
    dictation.setHotkey(&hotkey);

    kea::ModelDownloader downloader;
    kea::Readiness readiness(&settings, &inputMethod, &committer, &dictation);

    QObject::connect(&inputMethod, &kea::InputMethod::contextChanged,
                     &committer, [&](kea::InputMethodContext *ctx) {
                         committer.setContext(ctx);
                         readiness.refresh();
                     });

    QObject::connect(&dictation, &kea::DictationController::statusTextChanged, &tray, [&]() {
        tray.setStatusText(dictation.statusText());
    });
    QObject::connect(&dictation, &kea::DictationController::stateChanged, &tray, [&]() {
        tray.setListening(dictation.isListening());
    });

    QObject::connect(&tray, &TrayController::startRequested, &dictation, &kea::DictationController::start);
    QObject::connect(&tray, &TrayController::stopRequested, &dictation, &kea::DictationController::stop);
    QObject::connect(&tray, &TrayController::cancelRequested, &dictation, &kea::DictationController::cancel);

    QObject::connect(&settings, &kea::AppSettings::hotkeyChanged, &hotkey, [&]() {
        hotkey.setSequence(settings.hotkey());
    });

    QObject::connect(&inputMethod, &QWaylandClientExtension::activeChanged, &tray, [&]() {
        if (dictation.isListening()) {
            return;
        }
        if (inputMethod.isActive()) {
            tray.setStatusText(settings.modelFileExists()
                                   ? QStringLiteral("Idle — hold hotkey to dictate")
                                   : QStringLiteral("Idle — model missing (open Settings)"));
        } else {
            tray.setStatusText(QStringLiteral("Idle (input-method unavailable)"));
        }
        readiness.refresh();
    });

    if (inputMethod.isActive()) {
        tray.setStatusText(settings.modelFileExists()
                               ? QStringLiteral("Idle — hold hotkey to dictate")
                               : QStringLiteral("Idle — model missing (open Settings)"));
    } else if (!settings.modelFileExists()) {
        tray.setStatusText(QStringLiteral("Setup needed — open Settings"));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("_tray"), &tray);
    engine.rootContext()->setContextProperty(QStringLiteral("_committer"), &committer);
    engine.rootContext()->setContextProperty(QStringLiteral("_inputMethod"), &inputMethod);
    engine.rootContext()->setContextProperty(QStringLiteral("_dictation"), &dictation);
    engine.rootContext()->setContextProperty(QStringLiteral("_settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("_hotkey"), &hotkey);
    engine.rootContext()->setContextProperty(QStringLiteral("_readiness"), &readiness);
    engine.rootContext()->setContextProperty(QStringLiteral("_downloader"), &downloader);
    engine.rootContext()->setContextProperty(QStringLiteral("_defaultModelUrl"),
                                             QString::fromUtf8(kDefaultModelUrl));
    engine.rootContext()->setContextProperty(QStringLiteral("_defaultModelFilename"),
                                             QString::fromUtf8(kDefaultModelFilename));

    engine.loadFromModule("io.github.su3h7am.kea", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCCritical(keaLog) << "failed to load QML module io.github.su3h7am.kea";
        return -1;
    }

    // Do NOT eager-load the model on startup. The app starts idle so the user
    // can configure the model path and backend, then click Start to load.

    // Debug helper: KEA_AUTO_DICTATE_MS=N starts dictation after load and stops after N ms.
    const int autoMs = qEnvironmentVariableIntValue("KEA_AUTO_DICTATE_MS");
    if (autoMs > 0) {
        QObject::connect(&dictation, &kea::DictationController::modelLoadedChanged, &dictation, [&]() {
            if (!dictation.isModelLoaded()) {
                return;
            }
            qCInfo(keaLog) << "KEA_AUTO_DICTATE_MS: start for" << autoMs << "ms";
            dictation.start();
            QTimer::singleShot(autoMs, &dictation, &kea::DictationController::stop);
            QTimer::singleShot(autoMs + 60000, &app, &QCoreApplication::quit);
        }, Qt::SingleShotConnection);
    }

    return app.exec();
}
