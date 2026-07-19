/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * LlamaTransformer — on-device GGUF inference via llama.cpp (libllama).
 *
 * Built only when KEA_HAS_LLAMA is defined (KEA_BUILD_LLAMA=ON and the
 * ExternalProject produced headers/libs). Otherwise this class is a stub that
 * always reports "llama.cpp runtime not built into this Kea binary".
 */
#pragma once

#include "transform/text_transformer.h"

#include <memory>

namespace kea {

class LlamaTransformer : public TextTransformer
{
    Q_OBJECT
public:
    explicit LlamaTransformer(QObject *parent = nullptr);
    ~LlamaTransformer() override;

    QString backendName() const override { return QStringLiteral("llama.cpp"); }
    bool isReady() const override;
    bool loadModel(const QString &ggufPath) override;
    void unloadModel() override;
    QString transform(const QString &utterance, TransformStyle style) override;

    /// True when this binary was linked against libllama.
    static bool runtimeAvailable();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace kea
