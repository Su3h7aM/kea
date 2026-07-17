/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "input_method.h"

#include <QDebug>

namespace kea {

// --- InputMethod ----------------------------------------------------------

InputMethod::InputMethod(QObject *parent)
    : QWaylandClientExtensionTemplate<InputMethod>(1)
    , QtWayland::zwp_input_method_v1()
{
    Q_UNUSED(parent);
    // Start listening for the global; isActive() becomes true when KWin
    // advertises zwp_input_method_v1 and we successfully bind.
    initialize();

    connect(this, &QWaylandClientExtension::activeChanged, this, [this]() {
        if (!isActive()) {
            // Protocol lost (compositor restarted / IME conflict resolution).
            if (m_context) {
                m_context.reset();
                Q_EMIT contextChanged(nullptr);
                Q_EMIT deactivated();
            }
        } else {
            qInfo() << "Kea: input-method-v1 protocol bound";
        }
    });
}

InputMethod::~InputMethod() = default;

void InputMethod::zwp_input_method_v1_activate(struct ::zwp_input_method_context_v1 *id)
{
    // A text field gained focus. Destroy any previous context first.
    m_context.reset();
    m_context = std::make_unique<InputMethodContext>(id, this);
    Q_EMIT contextChanged(m_context.get());
    Q_EMIT activated();
}

void InputMethod::zwp_input_method_v1_deactivate(struct ::zwp_input_method_context_v1 *context)
{
    Q_UNUSED(context);
    if (!m_context) {
        return;
    }
    m_context.reset();
    Q_EMIT contextChanged(nullptr);
    Q_EMIT deactivated();
}

// --- InputMethodContext ---------------------------------------------------

InputMethodContext::InputMethodContext(struct ::zwp_input_method_context_v1 *object,
                                       QObject *parent)
    : QObject(parent)
    , QtWayland::zwp_input_method_context_v1(object)
    , m_serial(0)
    , m_valid(true)
{
}

InputMethodContext::~InputMethodContext()
{
    m_valid = false;
    // Destroy the wayland object if still live.
    if (object()) {
        destroy();
    }
}

bool InputMethodContext::isValid() const
{
    return m_valid && object() != nullptr;
}

void InputMethodContext::commitString(const QString &text)
{
    if (!isValid()) {
        return;
    }
    commit_string(m_serial, text);
}

void InputMethodContext::setPreedit(const QString &text, const QString &fallbackCommit)
{
    if (!isValid()) {
        return;
    }
    preedit_string(m_serial, text, fallbackCommit);
}

void InputMethodContext::zwp_input_method_context_v1_surrounding_text(const QString &text,
                                                                     uint32_t cursor,
                                                                     uint32_t anchor)
{
    Q_EMIT surroundingTextChanged(text, cursor, anchor);
}

void InputMethodContext::zwp_input_method_context_v1_reset()
{
    // Compositor asks us to drop composing state. Serial is unchanged.
}

void InputMethodContext::zwp_input_method_context_v1_content_type(uint32_t /*hint*/,
                                                                 uint32_t /*purpose*/)
{
}

void InputMethodContext::zwp_input_method_context_v1_invoke_action(uint32_t /*button*/,
                                                                  uint32_t /*index*/)
{
}

void InputMethodContext::zwp_input_method_context_v1_commit_state(uint32_t serial)
{
    m_serial = serial;
    Q_EMIT serialChanged(serial);
}

void InputMethodContext::zwp_input_method_context_v1_preferred_language(const QString & /*language*/)
{
}

} // namespace kea
