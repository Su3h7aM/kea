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
    height: Kirigami.Units.gridUnit * 36
    title: i18nc("@title:window", "Kea")

    // When set, the next successful download is treated as an LLM GGUF.
    property string _pendingLlmDownload: ""

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

                // Active model is always chosen from the catalog (bundled +
                // ~/.config/kea/models.json). Paths live in models.json via
                // quant.path — no free-text path field.
                Controls.ComboBox {
                    id: activeModelBox
                    Kirigami.FormData.label: i18nc("@label", "Active model")
                    Layout.fillWidth: true
                    model: _catalog ? _catalog.availableSelections : []
                    textRole: "display"
                    enabled: model && model.length > 0
                    Component.onCompleted: syncFromSettings()
                    onActivated: (index) => {
                        if (!_settings || index < 0 || index >= model.length)
                            return
                        const sel = model[index]
                        if (sel && sel.path)
                            _settings.modelPath = sel.path
                    }

                    function syncFromSettings() {
                        if (!_catalog || !_settings)
                            return
                        const idx = _catalog.indexOfAvailablePath(_settings.modelPath)
                        if (idx >= 0)
                            currentIndex = idx
                        else if (model && model.length > 0 && currentIndex < 0)
                            currentIndex = 0
                    }
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    opacity: 0.7
                    text: {
                        if (!_settings)
                            return ""
                        if (activeModelBox.model && activeModelBox.model.length === 0)
                            return i18nc("@info",
                                "No models available yet. Download one below, or add local paths in %1.",
                                _catalog ? _catalog.userCatalogPath : "~/.config/kea/models.json")
                        return _settings.modelPath
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
                    Kirigami.FormData.label: i18nc("@label", "Post-process")
                    text: i18nc("@option", "Polish transcript with local LLM")
                    checked: _settings ? _settings.postProcessEnabled : false
                    onToggled: {
                        if (_settings)
                            _settings.postProcessEnabled = checked
                    }
                }

                Controls.ComboBox {
                    id: styleBox
                    Kirigami.FormData.label: i18nc("@label", "Style")
                    enabled: _settings && _settings.postProcessEnabled
                    model: [
                        i18nc("@item", "Correct (default)"),
                        i18nc("@item", "Enhance"),
                        i18nc("@item", "Professional"),
                        i18nc("@item", "Casual")
                    ]
                    currentIndex: _settings ? _settings.postProcessStyle : 0
                    onActivated: (index) => {
                        if (_settings)
                            _settings.postProcessStyle = index
                    }
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    visible: _settings && _settings.postProcessEnabled
                    text: {
                        if (!_settings)
                            return ""
                        if (_settings.llmModelFileExists)
                            return i18nc("@info", "LLM model: %1", _settings.llmModelPath)
                        return i18nc("@info",
                            "Download an LLM model below (LFM2.5 230M recommended). Without it, raw ASR is used.")
                    }
                }

                Controls.Button {
                    text: i18nc("@action:button", "Mark setup complete")
                    visible: _settings && !_settings.onboardingDone
                    enabled: _readiness && _readiness.modelReady
                    onClicked: _settings.onboardingDone = true
                }
            }

            // --- Model catalog / download ---
            Kirigami.Heading {
                text: i18nc("@title:group", "Get a model")
                level: 2
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                enabled: (_dictation ? _dictation.canConfigure : true)
                         && !(_downloader && _downloader.busy)

                Controls.ComboBox {
                    id: catalogModelBox
                    Kirigami.FormData.label: i18nc("@label", "Model")
                    Layout.fillWidth: true
                    model: _catalog ? _catalog.models : []
                    textRole: "name"
                    Component.onCompleted: selectDefaultModel()
                    onCountChanged: {
                        if (currentIndex < 0)
                            selectDefaultModel()
                    }
                    onActivated: (index) => {
                        refreshQuants(index)
                    }

                    function selectDefaultModel() {
                        if (!_catalog || !_catalog.models)
                            return
                        const models = _catalog.models
                        const want = _catalog.defaultModelId()
                        for (let i = 0; i < models.length; ++i) {
                            if (models[i].id === want) {
                                currentIndex = i
                                refreshQuants(i)
                                return
                            }
                        }
                        if (models.length > 0) {
                            currentIndex = 0
                            refreshQuants(0)
                        }
                    }

                    function refreshQuants(index) {
                        if (!_catalog || index < 0 || index >= _catalog.models.length) {
                            quantBox.model = []
                            return
                        }
                        const mid = _catalog.models[index].id
                        quantBox.model = _catalog.quantsFor(mid)
                        const pref = _catalog.preferredQuant(mid)
                        let qi = 0
                        if (pref && pref.id) {
                            for (let i = 0; i < quantBox.model.length; ++i) {
                                if (quantBox.model[i].id === pref.id) {
                                    qi = i
                                    break
                                }
                            }
                        }
                        quantBox.currentIndex = qi
                    }

                    function selectedModel() {
                        if (!_catalog || currentIndex < 0 || currentIndex >= _catalog.models.length)
                            return null
                        return _catalog.models[currentIndex]
                    }
                }

                Controls.ComboBox {
                    id: quantBox
                    Kirigami.FormData.label: i18nc("@label", "Quantization")
                    Layout.fillWidth: true
                    model: []
                    textRole: "display"

                    function selectedQuant() {
                        if (currentIndex < 0 || currentIndex >= model.length)
                            return null
                        return model[currentIndex]
                    }
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.8
                    text: {
                        const m = catalogModelBox.selectedModel()
                        if (!m)
                            return ""
                        let t = m.description || ""
                        if (m.streaming)
                            t = i18nc("@info", "Streaming — live text while speaking.") + " " + t
                        else
                            t = i18nc("@info", "Offline — transcribes on hotkey release.") + " " + t
                        return t
                    }
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: {
                        const m = catalogModelBox.selectedModel()
                        const q = quantBox.selectedQuant()
                        if (!m || !q || !_catalog)
                            return ""
                        if (_catalog.isAvailable(m.id, q.id))
                            return i18nc("@info", "✓ Available at %1",
                                         _catalog.resolvedPath(m.id, q.id))
                        if (q.path)
                            return i18nc("@info", "Configured path missing: %1", q.path)
                        return i18nc("@info", "Not downloaded yet")
                    }
                }

                Controls.Button {
                    text: {
                        const m = catalogModelBox.selectedModel()
                        const q = quantBox.selectedQuant()
                        if (m && q && _catalog && _catalog.isAvailable(m.id, q.id))
                            return i18nc("@action:button", "Use this model")
                        if (q && !q.downloadable)
                            return i18nc("@action:button", "Path missing")
                        return i18nc("@action:button", "Download")
                    }
                    enabled: {
                        if (!_downloader || !_catalog)
                            return false
                        const m = catalogModelBox.selectedModel()
                        const q = quantBox.selectedQuant()
                        if (!m || !q)
                            return false
                        if (_catalog.isAvailable(m.id, q.id))
                            return true
                        return !!q.downloadable
                    }
                    onClicked: {
                        if (!_downloader || !_settings || !_catalog)
                            return
                        const m = catalogModelBox.selectedModel()
                        const q = quantBox.selectedQuant()
                        if (!m || !q)
                            return
                        if (_catalog.isAvailable(m.id, q.id)) {
                            _settings.modelPath = _catalog.resolvedPath(m.id, q.id)
                            activeModelBox.syncFromSettings()
                            return
                        }
                        if (!q.downloadable)
                            return
                        const dest = _catalog.downloadDest(m.id, q.id)
                        const size = q.sizeBytes ? Number(q.sizeBytes) : 0
                        const sha = q.sha256 ? String(q.sha256) : ""
                        _downloader.download(q.url, dest, sha, size)
                    }
                }
            }

            // Download controls stay enabled while busy (outside disabled form).
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                visible: _downloader && (_downloader.busy
                         || (_downloader.statusText && _downloader.statusText !== "Idle"))

                Controls.ProgressBar {
                    visible: _downloader && _downloader.busy
                    from: 0
                    to: 1
                    value: _downloader ? _downloader.progress : 0
                    Layout.fillWidth: true
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.8
                    text: _downloader ? _downloader.statusText : ""
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: _downloader && _downloader.lastError.length > 0
                    color: Kirigami.Theme.negativeTextColor
                    text: _downloader ? _downloader.lastError : ""
                }

                Controls.Button {
                    visible: _downloader && _downloader.busy
                    text: i18nc("@action:button", "Cancel download")
                    onClicked: {
                        if (_downloader)
                            _downloader.cancel()
                    }
                }
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.65
                text: {
                    const path = _catalog ? _catalog.userCatalogPath : "~/.config/kea/models.json"
                    return i18nc("@info",
                        "Point Kea at models you already have by editing %1. " +
                        "Use quant.path for an absolute (or ~/…) file path; " +
                        "those entries show up in Active model when the file exists. " +
                        "You can also add downloadable entries (url) that merge with the bundled catalog.",
                        path)
                }
            }

            // --- LLM post-process model download ---
            Kirigami.Heading {
                text: i18nc("@title:group", "Post-process LLM")
                level: 2
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                enabled: (_dictation ? _dictation.canConfigure : true)
                         && !(_downloader && _downloader.busy)

                Controls.ComboBox {
                    id: llmModelBox
                    Kirigami.FormData.label: i18nc("@label", "LLM model")
                    Layout.fillWidth: true
                    model: _llmCatalog ? _llmCatalog.models : []
                    textRole: "name"
                    Component.onCompleted: selectDefaultLlm()
                    onCountChanged: {
                        if (currentIndex < 0)
                            selectDefaultLlm()
                    }
                    onActivated: (index) => refreshLlmQuants(index)

                    function selectDefaultLlm() {
                        if (!_llmCatalog || !_llmCatalog.models)
                            return
                        const models = _llmCatalog.models
                        const want = _llmCatalog.defaultModelId()
                        for (let i = 0; i < models.length; ++i) {
                            if (models[i].id === want) {
                                currentIndex = i
                                refreshLlmQuants(i)
                                return
                            }
                        }
                        if (models.length > 0) {
                            currentIndex = 0
                            refreshLlmQuants(0)
                        }
                    }

                    function refreshLlmQuants(index) {
                        if (!_llmCatalog || index < 0 || index >= _llmCatalog.models.length) {
                            llmQuantBox.model = []
                            return
                        }
                        const mid = _llmCatalog.models[index].id
                        llmQuantBox.model = _llmCatalog.quantsFor(mid)
                        const pref = _llmCatalog.preferredQuant(mid)
                        let qi = 0
                        if (pref && pref.id) {
                            for (let i = 0; i < llmQuantBox.model.length; ++i) {
                                if (llmQuantBox.model[i].id === pref.id) {
                                    qi = i
                                    break
                                }
                            }
                        }
                        llmQuantBox.currentIndex = qi
                    }

                    function selectedModel() {
                        if (!_llmCatalog || currentIndex < 0 || currentIndex >= _llmCatalog.models.length)
                            return null
                        return _llmCatalog.models[currentIndex]
                    }
                }

                Controls.ComboBox {
                    id: llmQuantBox
                    Kirigami.FormData.label: i18nc("@label", "Quantization")
                    Layout.fillWidth: true
                    model: []
                    textRole: "display"
                    function selectedQuant() {
                        if (currentIndex < 0 || currentIndex >= model.length)
                            return null
                        return model[currentIndex]
                    }
                }

                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.75
                    text: {
                        const m = llmModelBox.selectedModel()
                        return m ? (m.description || "") : ""
                    }
                }

                Controls.Button {
                    text: {
                        const m = llmModelBox.selectedModel()
                        const q = llmQuantBox.selectedQuant()
                        if (m && q && _llmCatalog && _llmCatalog.isAvailable(m.id, q.id))
                            return i18nc("@action:button", "Use this LLM")
                        return i18nc("@action:button", "Download LLM")
                    }
                    enabled: {
                        if (!_downloader || !_llmCatalog)
                            return false
                        const m = llmModelBox.selectedModel()
                        const q = llmQuantBox.selectedQuant()
                        if (!m || !q)
                            return false
                        if (_llmCatalog.isAvailable(m.id, q.id))
                            return true
                        return !!q.downloadable
                    }
                    onClicked: {
                        if (!_downloader || !_settings || !_llmCatalog)
                            return
                        const m = llmModelBox.selectedModel()
                        const q = llmQuantBox.selectedQuant()
                        if (!m || !q)
                            return
                        if (_llmCatalog.isAvailable(m.id, q.id)) {
                            _settings.llmModelPath = _llmCatalog.resolvedPath(m.id, q.id)
                            return
                        }
                        const dest = _llmCatalog.downloadDest(m.id, q.id)
                        const size = q.sizeBytes ? Number(q.sizeBytes) : 0
                        const sha = q.sha256 ? String(q.sha256) : ""
                        // Tag path so onFinished can route to llmModelPath.
                        _downloader.download(q.url, dest, sha, size)
                        root._pendingLlmDownload = dest
                    }
                }
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.65
                text: {
                    const path = _llmCatalog ? _llmCatalog.userCatalogPath
                                             : "~/.config/kea/llm_models.json"
                    return i18nc("@info",
                        "Custom LLM entries: %1 (same schema; merge by model id). " +
                        "Files install under ~/.local/share/kea/llm-models/.",
                        path)
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: _catalog && _catalog.lastError.length > 0
                type: Kirigami.MessageType.Warning
                text: _catalog ? _catalog.lastError : ""
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
                    "Pick an Active model from the catalog, download one below, or set " +
                    "KEA_MODEL=/path/to/model.gguf. Offline models buffer until release; " +
                    "streaming models insert text live.\n\n" +
                    "Only one Wayland input method can own the seat — disable fcitx5/IBus if binding fails.")
            }
        }
    }

    Connections {
        target: _settings
        function onModelPathChanged() {
            activeModelBox.syncFromSettings()
        }
    }

    Connections {
        target: _catalog
        function onAvailabilityChanged() {
            activeModelBox.syncFromSettings()
            if (catalogModelBox.currentIndex >= 0)
                catalogModelBox.refreshQuants(catalogModelBox.currentIndex)
        }
    }

    Connections {
        target: _downloader
        function onFinished(localPath) {
            if (root._pendingLlmDownload && localPath === root._pendingLlmDownload) {
                root._pendingLlmDownload = ""
                if (_settings)
                    _settings.llmModelPath = localPath
                if (_llmCatalog)
                    _llmCatalog.refreshAvailability()
                if (llmModelBox.currentIndex >= 0)
                    llmModelBox.refreshLlmQuants(llmModelBox.currentIndex)
                return
            }
            if (_settings)
                _settings.modelPath = localPath
            if (_catalog)
                _catalog.refreshAvailability()
            if (catalogModelBox.currentIndex >= 0)
                catalogModelBox.refreshQuants(catalogModelBox.currentIndex)
            activeModelBox.syncFromSettings()
        }
    }
}
