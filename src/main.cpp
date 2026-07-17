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
#include <QUrl>
#include <QVector>

#include <KAboutData>
#include <KIconTheme>
#include <KLocalizedContext>
#include <KLocalizedString>

#include "app/app_settings.h"
#include "app/tray_controller.h"
#include "controller/dictation_controller.h"
#include "hotkey/global_hotkey.h"
#include "insert/input_method.h"
#include "insert/text_committer.h"

int main(int argc, char *argv[])
{
    // Queued cross-thread PCM delivery (main → worker).
    qRegisterMetaType<QVector<float>>("QVector<float>");

    KIconTheme::initTheme();

    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kea");

    QApplication::setOrganizationName(QStringLiteral("KDE"));
    QApplication::setOrganizationDomain(QStringLiteral("kde.org"));
    QApplication::setApplicationName(QStringLiteral("Kea"));
    QApplication::setDesktopFileName(QStringLiteral("org.kde.kea"));
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
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("preferences-desktop-locale")));

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

    // Input-method context lifecycle → committer + tray status (when idle).
    QObject::connect(&inputMethod, &kea::InputMethod::contextChanged,
                     &committer, [&](kea::InputMethodContext *ctx) {
                         committer.setContext(ctx);
                     });

    // Dictation status drives the tray.
    QObject::connect(&dictation, &kea::DictationController::statusTextChanged, &tray, [&]() {
        tray.setStatusText(dictation.statusText());
    });
    QObject::connect(&dictation, &kea::DictationController::stateChanged, &tray, [&]() {
        tray.setListening(dictation.isListening());
    });

    // Tray actions.
    QObject::connect(&tray, &TrayController::startRequested, &dictation, &kea::DictationController::start);
    QObject::connect(&tray, &TrayController::stopRequested, &dictation, &kea::DictationController::stop);
    QObject::connect(&tray, &TrayController::cancelRequested, &dictation, &kea::DictationController::cancel);

    // Settings → hotkey rebind.
    QObject::connect(&settings, &kea::AppSettings::hotkeyChanged, &hotkey, [&]() {
        hotkey.setSequence(settings.hotkey());
    });

    // Reflect input-method availability when not actively dictating.
    QObject::connect(&inputMethod, &QWaylandClientExtension::activeChanged, &tray, [&]() {
        if (dictation.isListening()) {
            return;
        }
        if (inputMethod.isActive()) {
            tray.setStatusText(QStringLiteral("Idle — hold hotkey to dictate"));
        } else {
            tray.setStatusText(QStringLiteral("Idle (input-method unavailable)"));
        }
    });
    if (inputMethod.isActive()) {
        tray.setStatusText(QStringLiteral("Idle — hold hotkey to dictate"));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("_tray"), &tray);
    engine.rootContext()->setContextProperty(QStringLiteral("_committer"), &committer);
    engine.rootContext()->setContextProperty(QStringLiteral("_inputMethod"), &inputMethod);
    engine.rootContext()->setContextProperty(QStringLiteral("_dictation"), &dictation);
    engine.rootContext()->setContextProperty(QStringLiteral("_settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("_hotkey"), &hotkey);

    engine.loadFromModule("org.kde.kea", "Main");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // Eager-load the model in the background so the first hotkey is fast.
    dictation.loadModel();

    return app.exec();
}
