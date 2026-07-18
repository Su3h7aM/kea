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
    // Drop the old context carefully: notify TextCommitter *before* destroying
    // so it never holds a dangling IInputContext* (use-after-free → SIGSEGV).
    if (m_context) {
        Q_EMIT contextChanged(nullptr);
        m_context.reset();
    }
    m_context = std::make_unique<InputMethodContext>(id, this);
    // KWin only sends activate when a client enables text-input on a surface.
    qInfo() << "Kea: input-method activate — client enabled text-input (new context)";
    Q_EMIT contextChanged(m_context.get());
    Q_EMIT activated();
}

void InputMethod::zwp_input_method_v1_deactivate(struct ::zwp_input_method_context_v1 *context)
{
    Q_UNUSED(context);
    if (!m_context) {
        return;
    }
    qInfo().nospace() << "Kea: input-method deactivate — text-input session ended"
                      << " serial=" << m_context->serial()
                      << " sawCommitState=" << m_context->hasReceivedCommitState()
                      << " purpose=" << m_context->contentPurpose()
                      << " hint=" << m_context->contentHint();
    // Notify first, destroy second — same UAF rule as activate.
    Q_EMIT contextChanged(nullptr);
    Q_EMIT deactivated();
    m_context.reset();
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
    // Destroy the wayland object if still live. Guard against double-destroy.
    if (object()) {
        destroy();
    }
}

bool InputMethodContext::isValid() const
{
    // RFC R2: do not treat the context as ready until the compositor has sent
    // at least one commit_state (serial known). Serial defaults to 0 on the
    // wire before that, but committing with an unknown serial is unsafe.
    return m_valid && object() != nullptr && m_sawCommitState;
}

void InputMethodContext::commitString(const QString &text)
{
    if (!isValid() || text.isEmpty()) {
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

void InputMethodContext::zwp_input_method_context_v1_content_type(uint32_t hint,
                                                                 uint32_t purpose)
{
    m_contentHint = hint;
    m_contentPurpose = purpose;
    qInfo().nospace() << "Kea: input-method content_type hint=" << hint
                      << " purpose=" << purpose;
    Q_EMIT contentTypeChanged(hint, purpose);
}

void InputMethodContext::zwp_input_method_context_v1_invoke_action(uint32_t /*button*/,
                                                                  uint32_t /*index*/)
{
}

void InputMethodContext::zwp_input_method_context_v1_commit_state(uint32_t serial)
{
    m_serial = serial;
    m_sawCommitState = true;
    Q_EMIT serialChanged(serial);
}

void InputMethodContext::zwp_input_method_context_v1_preferred_language(const QString & /*language*/)
{
}

} // namespace kea
