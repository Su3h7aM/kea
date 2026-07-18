/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Main — Kea settings, readiness, onboarding, and live dictation status.
 *
 * Workflow:
 *   1. Set model path + backend (editable when model is NOT loaded).
 *   2. Click Start → loads the model, locks settings, enables PTT.
 *   3. Hold the hotkey to dictate; release to commit.
 *   4. Click Stop → unloads the model, unlocks settings so you can change
 *      the backend or model file.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.kde.kquickcontrols as KQuickControls

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
                // Start = load model + become ready; Stop = unload model.
                text: _dictation && _dictation.modelLoaded
                      ? i18nc("@action", "Stop")
                      : i18nc("@action", "Start")
                icon.name: _dictation && _dictation.modelLoaded
                           ? "media-playback-stop"
                           : "media-playback-start"
                enabled: _dictation && !_dictation.canConfigure ? true   // Stop always works when loaded
                         : (_readiness && _readiness.modelReady)          // Start needs a model file
                onTriggered: {
                    if (_dictation.modelLoaded)
                        _dictation.unloadModel()
                    else
                        _dictation.loadModel()
                }
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

            // --- Settings (disabled while the model is loaded) ---
            Kirigami.Heading {
                text: i18nc("@title:group", "Settings")
                level: 2
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                enabled: _dictation ? _dictation.canConfigure : true

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

                Controls.ComboBox {
                    Kirigami.FormData.label: i18nc("@label", "Activation")
                    model: [i18nc("@item", "Push to talk (hold)"),
                            i18nc("@item", "Toggle (press to start/stop)")]
                    currentIndex: _settings ? _settings.activationMode : 0
                    onActivated: (index) => {
                        if (_settings)
                            _settings.activationMode = index
                    }
                }

                Controls.CheckBox {
                    Kirigami.FormData.label: i18nc("@label", "Clipboard (last resort)")
                    text: i18nc("@option",
                        "If the focused app never opens a text-input session " +
                        "(some terminals), copy the transcript so you can paste. " +
                        "Qt/GTK/Firefox usually insert via the compositor input method.")
                    checked: _settings ? _settings.clipboardFallback : true
                    onToggled: {
                        if (_settings)
                            _settings.clipboardFallback = checked
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

            // --- Hotkey (always editable — independent of model state) ---
            Kirigami.FormLayout {
                Layout.fillWidth: true

                // Capture the real key chord (same control class as System Settings),
                // instead of free-typed strings that QKeySequence::fromString may reject.
                KQuickControls.KeySequenceItem {
                    id: hotkeyItem
                    Kirigami.FormData.label: i18nc("@label", "Hotkey")
                    // Declarative only — do not assign keySequence imperatively
                    // (that would break this binding). User edits go out via the signal.
                    keySequence: _hotkey ? _hotkey.sequence : ""
                    // Multi-key chords are unusual for hold-to-talk; keep single sequence.
                    multiKeyShortcutsAllowed: false
                    onKeySequenceModified: {
                        if (!_settings)
                            return
                        // Empty sequence = hotkey cleared/disabled (not reset to default).
                        _settings.hotkeySequence = hotkeyItem.keySequence
                    }
                }

                Controls.Label {
                    Kirigami.FormData.label: i18nc("@label", "Registered")
                    text: _hotkey && _hotkey.registered
                          ? i18nc("@info", "✓ Yes")
                          : i18nc("@info", "✗ No — check System Settings")
                    opacity: 0.7
                }
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: i18nc("@info",
                    "Click Start to load the model, then hold the global hotkey " +
                    "(default Ctrl+Shift+D) while a text field is focused to dictate. " +
                    "Release to commit.\n\n" +
                    "Click Stop to unload the model and change the backend or model file.\n\n" +
                    "Set the model path above, or export KEA_MODEL=/path/to/model.gguf. " +
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
        }
    }
}
