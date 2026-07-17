/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Main — the Kea settings window shell.
 *
 * Phase 0: a Kirigami ApplicationWindow with a single landing page so the
 * window + tray wiring can be exercised. The real settings pages (hotkey,
 * model, device, backend) land in later phases.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    width: Kirigami.Units.gridUnit * 28
    height: Kirigami.Units.gridUnit * 20
    title: i18nc("@title:window", "Kea")

    // The tray exposes itself as `_tray`. Toggling the tray's "Settings" item
    // raises this window; closing it keeps the process alive (tray-owned).
    Connections {
        target: _tray
        function onShowWindowRequested() {
            root.show()
            root.raise()
            root.requestActivate()
        }
    }

    onClosing: function (close) {
        // Tray app: hide instead of quitting so dictation stays available.
        close.accepted = false
        root.hide()
    }

    pageStack.initialPage: Kirigami.Page {
        title: i18nc("@title", "Kea")

        Kirigami.FormLayout {
            anchors.fill: parent

            Controls.Label {
                Kirigami.FormData.label: i18nc("@label", "Status")
                text: _tray ? _tray.statusText : ""
            }

            Controls.Label {
                Kirigami.FormData.label: i18nc("@label", "Version")
                text: "0.1.0 (pre-alpha)"
            }
        }
    }
}
