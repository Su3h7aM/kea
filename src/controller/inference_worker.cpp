/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * Translation unit so AUTOMOC generates meta-object code for InferenceWorker.
 */
#include "inference_worker.h"

namespace kea {

// Pure interface — no out-of-line methods. Virtual destructor is defaulted
// via QObject. This .cpp exists only so moc runs on the header's Q_OBJECT.

} // namespace kea
