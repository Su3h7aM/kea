/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Main — Kea settings, readiness, onboarding, and live dictation status.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    width: Kirigami.Units.gridUnit * 34
    height: Kirigami.Units.gridUnit * 32
    title: i18nc("@title:window", "Kea")

    // Show settings on first launch so the user can complete onboarding.
    Component.onCompleted: {
        if (_settings && !_settings.onboardingDone) {
            root.show()
        }
    }

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
                enabled: _readiness ? (_readiness.modelReady || (_dictation && _dictation.listening)) : true
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

            // --- Onboarding / readiness banner ---
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: _readiness && !_readiness.readyToDictate
                type: Kirigami.MessageType.Information
                text: _readiness ? _readiness.summary : ""
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: _dictation && _dictation.lastError.length > 0
                type: Kirigami.MessageType.Error
                text: _dictation ? _dictation.lastError : ""
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: _settings && !_settings.onboardingDone
                type: Kirigami.MessageType.Positive
                text: i18nc("@info",
                    "Welcome to Kea. Complete the checklist below, then mark setup done.")
            }

            // --- Setup checklist ---
            Kirigami.FormLayout {
                Layout.fillWidth: true

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "1. Model file")
                    text: _readiness && _readiness.modelReady
                          ? i18nc("@info", "✓ Found")
                          : i18nc("@info", "✗ Missing")
                }
                Controls.Label {
                    text: _readiness ? _readiness.modelHint : ""
                    wrapMode: Text.WrapAnywhere
                    opacity: 0.7
                    Layout.fillWidth: true
                }

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "2. Input method")
                    text: _readiness && _readiness.inputMethodBound
                          ? i18nc("@info", "✓ Bound")
                          : i18nc("@info", "✗ Not bound")
                }
                Controls.Label {
                    text: _readiness ? _readiness.inputMethodHint : ""
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    Layout.fillWidth: true
                }

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "3. Text field")
                    text: _readiness && _readiness.textFieldActive
                          ? i18nc("@info", "✓ Focused")
                          : i18nc("@info", "○ Focus any text field when ready")
                }
            }

            Kirigami.Separator { Layout.fillWidth: true }

            // --- Live status ---
            Kirigami.Heading {
                text: i18nc("@title:group", "Dictation")
                level: 2
            }

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
                    Kirigami.FormData.label: i18nc("@label", "Hotkey")
                    text: _hotkey ? _hotkey.sequenceDisplay : ""
                }

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "Engine model")
                    text: {
                        if (!_dictation)
                            return ""
                        return _dictation.modelLoaded
                               ? i18nc("@info", "Loaded in memory")
                               : i18nc("@info", "Not loaded")
                    }
                }
            }

            Kirigami.Separator { Layout.fillWidth: true }

            // --- Settings ---
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
                    enabled: _downloader ? !_downloader.busy : true
                    onClicked: {
                        if (_settings)
                            _settings.modelPath = modelField.text
                        if (_dictation)
                            _dictation.loadModel()
                    }
                }

                Controls.Button {
                    text: _downloader && _downloader.busy
                          ? i18nc("@action:button", "Cancel download")
                          : i18nc("@action:button", "Download TDT 0.6B v3 (HF)")
                    enabled: _downloader !== null
                    onClicked: {
                        if (!_downloader || !_settings)
                            return
                        if (_downloader.busy) {
                            _downloader.cancel()
                            return
                        }
                        const dest = _settings.modelsDir() + "/" + _defaultModelFilename
                        _downloader.download(_defaultModelUrl, dest)
                    }
                }

                Controls.ProgressBar {
                    visible: _downloader && _downloader.busy
                    from: 0
                    to: 1
                    value: _downloader ? _downloader.progress : 0
                    Layout.fillWidth: true
                }

                Controls.Label {
                    visible: _downloader && _downloader.statusText.length > 0
                    text: _downloader ? _downloader.statusText : ""
                    opacity: 0.8
                }

                Controls.Button {
                    text: i18nc("@action:button", "Mark setup complete")
                    visible: _settings && !_settings.onboardingDone
                    enabled: _readiness && _readiness.modelReady
                    onClicked: _settings.onboardingDone = true
                }
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: i18nc("@info",
                    "Hold the global hotkey (default Meta+Ctrl+X) while a text field " +
                    "is focused to dictate. Release to commit.\n\n" +
                    "Set the model path in Settings, or export KEA_MODEL=/path/to/model.gguf. " +
                    "Offline models (e.g. TDT) buffer audio until release; streaming EOU " +
                    "models insert text live.\n\n" +
                    "Only one Wayland input method can own the seat — disable fcitx5/IBus if binding fails.")
            }
        }
    }

    Connections {
        target: _downloader
        function onFinished(localPath) {
            if (_settings) {
                _settings.modelPath = localPath
                modelField.text = localPath
            }
            if (_dictation)
                _dictation.loadModel()
        }
    }
}
