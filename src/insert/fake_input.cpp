/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "fake_input.h"

#include <QDebug>
#include <QVector>

#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

namespace kea {

FakeInputClient::FakeInputClient(QObject *parent)
    : QWaylandClientExtensionTemplate<FakeInputClient>(6) // keyboard_keysym
    , QtWayland::org_kde_kwin_fake_input()
{
    Q_UNUSED(parent);
    initialize();

    connect(this, &QWaylandClientExtension::activeChanged, this, [this]() {
        if (!isActive()) {
            m_authenticated = false;
            qInfo() << "Kea: fake_input protocol lost";
        } else {
            qInfo() << "Kea: fake_input protocol bound (org_kde_kwin_fake_input)";
        }
        Q_EMIT availabilityChanged(isActive());
    });
}

FakeInputClient::~FakeInputClient() = default;

void FakeInputClient::ensureAuthenticated()
{
    if (m_authenticated || !isActive()) {
        return;
    }
    // KWin may prompt or silently allow/deny; the protocol gives no reply.
    authenticate(QStringLiteral("Kea"),
                 QStringLiteral("Insert dictated text into the focused application"));
    m_authenticated = true;
    qInfo() << "Kea: fake_input authenticate() sent (compositor may ignore events)";
}

bool FakeInputClient::typeText(const QString &text)
{
    if (text.isEmpty()) {
        return true;
    }
    if (!isActive()) {
        qWarning() << "Kea: fake_input unavailable (not bound — not on KWin?)";
        return false;
    }

    // Resolve all keysyms first so we never partially type then fall back.
    const QVector<uint> ucs4 = text.toUcs4();
    QVector<xkb_keysym_t> keysyms;
    keysyms.reserve(ucs4.size());
    for (uint uc : ucs4) {
        const xkb_keysym_t ks = xkb_utf32_to_keysym(uc);
        if (ks == XKB_KEY_NoSymbol) {
            qWarning().nospace() << "Kea: fake_input no keysym for U+"
                                 << Qt::hex << uc << Qt::dec
                                 << " — not typing via fake_input";
            return false;
        }
        keysyms.push_back(ks);
    }

    ensureAuthenticated();

    for (xkb_keysym_t ks : keysyms) {
        keyboard_keysym(static_cast<uint32_t>(ks), WL_KEYBOARD_KEY_STATE_PRESSED);
        keyboard_keysym(static_cast<uint32_t>(ks), WL_KEYBOARD_KEY_STATE_RELEASED);
    }

    qInfo().nospace() << "Kea: fake_input typed chars=" << text.size()
                      << " keysyms=" << keysyms.size();
    return true;
}

} // namespace kea
