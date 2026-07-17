/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Kea — entry point.
 *
 * Boots a Qt/Kirigami application, registers the i18n context, wires up the
 * system-tray controller, and loads the QML module (org.kde.kea.Main).
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

    // System-tray presence. Owns its KStatusNotifierItem; shows the window on
    // activate and quits the app from its menu. Registered as a context
    // property so the QML side can reflect tray state and toggle visibility.
    TrayController tray;
    tray.show();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("_tray"), &tray);

    engine.loadFromModule("org.kde.kea", "Main");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
