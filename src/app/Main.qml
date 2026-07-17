/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Main — the Kea settings window shell.
 *
 * Phase 2: shows tray status + input-method availability so the insertion
 * path can be exercised. Real settings pages land in later phases.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    width: Kirigami.Units.gridUnit * 28
    height: Kirigami.Units.gridUnit * 22
    title: i18nc("@title:window", "Kea")

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
                Kirigami.FormData.label: i18nc("@label", "Input method")
                text: {
                    if (!_inputMethod)
                        return i18nc("@info", "Unavailable")
                    if (!_inputMethod.active)
                        return i18nc("@info", "Not bound (need Wayland + free IME slot)")
                    if (_committer && _committer.canCommit)
                        return i18nc("@info", "Active — text field focused")
                    return i18nc("@info", "Bound — focus a text field to activate")
                }
            }

            Controls.Label {
                Kirigami.FormData.label: i18nc("@label", "Version")
                text: "0.1.0 (pre-alpha)"
            }

            Controls.Label {
                Layout.columnSpan: 2
                wrapMode: Text.WordWrap
                text: i18nc("@info",
                    "Kea uses the Wayland input-method protocol to insert text. " +
                    "Only one input method can own the seat — disable fcitx5/IBus " +
                    "if binding fails.")
                opacity: 0.7
            }
        }
    }
}
