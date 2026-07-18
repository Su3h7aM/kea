/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * KWin org_kde_kwin_fake_input — privileged key injection (Plasma only).
 *
 * Used as step 2 in InsertionRouter when input-method-v1 has no text-input
 * context. Compositor may ignore all events after authenticate(); there is no
 * success signal. We best-effort type keysyms and still fall through to
 * clipboard if this path is unavailable.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QWaylandClientExtension>

#include "qwayland-fake-input.h"

namespace kea {

class FakeInputClient
    : public QWaylandClientExtensionTemplate<FakeInputClient>
    , public QtWayland::org_kde_kwin_fake_input
{
    Q_OBJECT

public:
    explicit FakeInputClient(QObject *parent = nullptr);
    ~FakeInputClient() override;

    /// True when KWin advertised and we bound the global.
    bool protocolAvailable() const { return isActive(); }

    /// Type `text` as keyboard keysym press/release pairs. Returns false if
    /// the protocol is missing or a character has no keysym (caller should
    /// try the next insert path without having partially typed).
    bool typeText(const QString &text);

Q_SIGNALS:
    void availabilityChanged(bool available);

private:
    void ensureAuthenticated();

    bool m_authenticated = false;
};

} // namespace kea
