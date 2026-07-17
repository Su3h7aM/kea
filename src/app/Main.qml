/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Main — Kea settings + live dictation status.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    width: Kirigami.Units.gridUnit * 32
    height: Kirigami.Units.gridUnit * 28
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
        close.accepted = false
        root.hide()
    }

    pageStack.initialPage: Kirigami.ScrollablePage {
        title: i18nc("@title", "Kea")

        actions: [
            Kirigami.Action {
                text: _dictation && _dictation.listening
                      ? i18nc("@action", "Stop")
                      : i18nc("@action", "Start")
                icon.name: _dictation && _dictation.listening ? "media-playback-stop" : "media-record"
                onTriggered: {
                    if (_dictation.listening)
                        _dictation.stop()
                    else
                        _dictation.start()
                }
            },
            Kirigami.Action {
                text: i18nc("@action", "Cancel")
                icon.name: "dialog-cancel"
                enabled: _dictation && _dictation.state !== 0
                onTriggered: _dictation.cancel()
            }
        ]

        ColumnLayout {
            width: parent.width
            spacing: Kirigami.Units.largeSpacing

            Kirigami.FormLayout {
                Layout.fillWidth: true

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "State")
                    text: _dictation ? _dictation.stateName : ""
                }

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "Status")
                    text: _dictation ? _dictation.statusText : ""
                    wrapMode: Text.WordWrap
                }

                Controls.ProgressBar {
                    Kirigami.FormData.label: i18nc("@label", "Level")
                    from: 0
                    to: 1
                    value: _dictation ? _dictation.level : 0
                    Layout.fillWidth: true
                }

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "Model")
                    text: {
                        if (!_dictation)
                            return ""
                        return _dictation.modelLoaded
                               ? i18nc("@info", "Loaded")
                               : i18nc("@info", "Not loaded")
                    }
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
                        return i18nc("@info", "Bound — focus a text field")
                    }
                    wrapMode: Text.WordWrap
                }

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "Hotkey")
                    text: _hotkey ? _hotkey.sequenceDisplay : ""
                }

                Controls.Label {
                    visible: _dictation && _dictation.lastError.length > 0
                    Kirigami.FormData.label: i18nc("@label", "Error")
                    text: _dictation ? _dictation.lastError : ""
                    color: Kirigami.Theme.negativeTextColor
                    wrapMode: Text.WordWrap
                }
            }

            Kirigami.Separator { Layout.fillWidth: true }

            Kirigami.Heading {
                text: i18nc("@title:group", "Settings")
                level: 2
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true

                Controls.TextField {
                    id: modelField
                    Kirigami.FormData.label: i18nc("@label", "Model path")
                    text: _settings ? _settings.modelPath : ""
                    Layout.fillWidth: true
                    onEditingFinished: {
                        if (_settings)
                            _settings.modelPath = text
                    }
                }

                Controls.ComboBox {
                    Kirigami.FormData.label: i18nc("@label", "Backend")
                    model: [i18nc("@item", "CPU"), i18nc("@item", "Vulkan")]
                    currentIndex: _settings ? _settings.backend : 0
                    onActivated: (index) => {
                        if (_settings)
                            _settings.backend = index
                    }
                }

                Controls.Button {
                    text: i18nc("@action:button", "Reload model")
                    onClicked: {
                        if (_settings)
                            _settings.modelPath = modelField.text
                        if (_dictation)
                            _dictation.loadModel()
                    }
                }
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: i18nc("@info",
                    "Hold the global hotkey (default Meta+Shift+V) while a text field " +
                    "is focused to dictate. Release to commit. Tray click also starts/stops. " +
                    "Only one Wayland input method can own the seat — disable fcitx5/IBus if binding fails.")
            }
        }
    }
}
