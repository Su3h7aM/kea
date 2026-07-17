/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Kea — entry point.
 *
 * Boots a Qt/Kirigami application, registers the i18n context, wires up the
 * system-tray controller and Wayland input-method text insertion, and loads
 * the QML module (org.kde.kea.Main).
 */
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include <KAboutData>
#include <KIconTheme>
#include <KLocalizedContext>
#include <KLocalizedString>

#include "app/tray_controller.h"
#include "insert/input_method.h"
#include "insert/text_committer.h"

int main(int argc, char *argv[])
{
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

    // System-tray presence.
    TrayController tray;
    tray.show();

    // Wayland text insertion (input-method-v1). Binds the global if the
    // compositor advertises it; on activate/deactivate the TextCommitter
    // receives the live IInputContext. On non-Wayland platforms (or when
    // another IME owns the seat) this stays inactive.
    kea::InputMethod inputMethod;
    kea::TextCommitter committer;
    QObject::connect(&inputMethod, &kea::InputMethod::contextChanged,
                     &committer, [&](kea::InputMethodContext *ctx) {
                         committer.setContext(ctx);
                         if (ctx) {
                             tray.setStatusText(QStringLiteral("Ready (text field focused)"));
                         } else if (inputMethod.protocolAvailable()) {
                             tray.setStatusText(QStringLiteral("Idle (focus a text field)"));
                         } else {
                             tray.setStatusText(QStringLiteral("Idle (input-method unavailable)"));
                         }
                     });
    QObject::connect(&inputMethod, &QWaylandClientExtension::activeChanged, &tray, [&]() {
        if (inputMethod.isActive()) {
            tray.setStatusText(QStringLiteral("Idle (focus a text field)"));
        } else {
            tray.setStatusText(QStringLiteral("Idle (input-method unavailable)"));
        }
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("_tray"), &tray);
    engine.rootContext()->setContextProperty(QStringLiteral("_committer"), &committer);
    engine.rootContext()->setContextProperty(QStringLiteral("_inputMethod"), &inputMethod);

    engine.loadFromModule("org.kde.kea", "Main");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
