/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Wayland input-method client for KWin / Plasma.
 *
 * KWin implements input_method_unstable_v1 (see KWin's InputMethodV1Interface),
 * not v2. There is one input method object per seat — if fcitx5/IBus already
 * owns the slot, Kea cannot bind (RFC risk R1).
 *
 * Flow:
 *   1. Bind zwp_input_method_v1 (global).
 *   2. On activate → receive a new zwp_input_method_context_v1.
 *   3. Track serial via commit_state events on the context.
 *   4. commit_string(serial, text) / preedit_string(serial, text, commit).
 *   5. On deactivate → destroy the context.
 */
#pragma once

#include <QObject>
#include <memory>

#include <QWaylandClientExtension>

#include "input_context.h"
#include "qwayland-input-method-unstable-v1.h"

namespace kea {

class InputMethodContext;

/// Binds zwp_input_method_v1 and owns the active InputMethodContext.
class InputMethod
    : public QWaylandClientExtensionTemplate<InputMethod>
    , public QtWayland::zwp_input_method_v1
{
    Q_OBJECT

public:
    explicit InputMethod(QObject *parent = nullptr);
    ~InputMethod() override;

    /// The live context while a text field is focused; nullptr otherwise.
    InputMethodContext *activeContext() const { return m_context.get(); }

    /// True when the compositor advertised and we bound the global.
    bool protocolAvailable() const { return isActive(); }

Q_SIGNALS:
    void contextChanged(InputMethodContext *ctx);
    void activated();
    void deactivated();

protected:
    void zwp_input_method_v1_activate(struct ::zwp_input_method_context_v1 *id) override;
    void zwp_input_method_v1_deactivate(struct ::zwp_input_method_context_v1 *context) override;

private:
    std::unique_ptr<InputMethodContext> m_context;
};

/// Per-focus text-input session. Implements IInputContext for TextCommitter.
class InputMethodContext
    : public QObject
    , public QtWayland::zwp_input_method_context_v1
    , public IInputContext
{
    Q_OBJECT

public:
    explicit InputMethodContext(struct ::zwp_input_method_context_v1 *object,
                                QObject *parent = nullptr);
    ~InputMethodContext() override;

    // IInputContext
    bool isValid() const override;
    uint32_t serial() const override { return m_serial; }
    void commitString(const QString &text) override;
    void setPreedit(const QString &text, const QString &fallbackCommit) override;

    /// Last content_type from the client (diagnostics; 0 if never sent).
    uint32_t contentHint() const { return m_contentHint; }
    uint32_t contentPurpose() const { return m_contentPurpose; }
    bool hasReceivedCommitState() const { return m_sawCommitState; }

Q_SIGNALS:
    void serialChanged(uint32_t serial);
    void surroundingTextChanged(const QString &text, uint32_t cursor, uint32_t anchor);
    void contentTypeChanged(uint32_t hint, uint32_t purpose);

protected:
    void zwp_input_method_context_v1_surrounding_text(const QString &text,
                                                      uint32_t cursor,
                                                      uint32_t anchor) override;
    void zwp_input_method_context_v1_reset() override;
    void zwp_input_method_context_v1_content_type(uint32_t hint, uint32_t purpose) override;
    void zwp_input_method_context_v1_invoke_action(uint32_t button, uint32_t index) override;
    void zwp_input_method_context_v1_commit_state(uint32_t serial) override;
    void zwp_input_method_context_v1_preferred_language(const QString &language) override;

private:
    uint32_t m_serial = 0;
    bool m_valid = true;
    bool m_sawCommitState = false;
    uint32_t m_contentHint = 0;
    uint32_t m_contentPurpose = 0;
};

} // namespace kea
