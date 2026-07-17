/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Abstract input-method context used by TextCommitter.
 *
 * Separating the transport from the serial/commit logic lets us unit-test the
 * committer without a live Wayland compositor (see text_committer_test.cpp).
 * The Wayland-backed implementation is InputMethodContext in input_method.*.
 */
#pragma once

#include <QString>
#include <cstdint>

namespace kea {

class IInputContext
{
public:
    virtual ~IInputContext() = default;

    /// True while a text field is focused and this context is live.
    virtual bool isValid() const = 0;

    /// Latest serial from the compositor (commit_state on v1).
    virtual uint32_t serial() const = 0;

    /// Commit finalized text into the focused field.
    virtual void commitString(const QString &text) = 0;

    /// Show an inline preedit (composing) string. `fallbackCommit` is what the
    /// client inserts if the preedit is reset (unfocus mid-compose); pass "" for
    /// dictation (we don't want partial words stuck on cancel).
    virtual void setPreedit(const QString &text, const QString &fallbackCommit = {}) = 0;
};

} // namespace kea
