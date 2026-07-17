/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * AudioRecorder — captures microphone audio and emits 16 kHz mono float PCM.
 *
 * Wraps Qt6 Multimedia's QAudioSource. On Linux that resolves to the native
 * PipeWire backend (Qt >= 6.10). PipeWire typically delivers 48 kHz; we capture
 * as 16-bit mono and run each chunk through a StreamingResampler to produce the
 * 16 kHz mono float32 blocks parakeet's streaming path expects.
 *
 * Pull mode: QAudioSource gives us a QIODevice; we read on readyRead. Output is
 * emitted as the pcmBlock signal. A levelChanged signal (RMS) drives the UI
 * level meter. This class runs its capture on the owning thread — the
 * DictationController (Phase 3) will marshal pcmBlock onto the parakeet worker
 * thread, respecting parakeet's per-context thread-affinity invariant.
 */
#pragma once

#include <QObject>
#include <QVector>

#include <memory>

class QAudioSource;
class QIODevice;

namespace kea {

class StreamingResampler;

class AudioRecorder : public QObject
{
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    /// Begin capturing from the default input device. Returns false if there is
    /// no input device or the format is unsupported.
    bool start();

    /// Stop capturing.
    void stop();

    bool isActive() const { return m_source != nullptr; }

Q_SIGNALS:
    /// A block of 16 kHz mono float32 PCM, ready for parakeet stream_feed.
    void pcmBlock(const QVector<float> &samples);
    /// Input level (RMS, 0..1) for the UI meter.
    void levelChanged(float level);

private:
    void onReadyRead();

    QAudioSource *m_source = nullptr;
    QIODevice *m_io = nullptr;
    std::unique_ptr<StreamingResampler> m_resampler;
    int m_inRate = 48000;
};

} // namespace kea
