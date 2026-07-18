/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * AudioRecorder — captures microphone audio and emits 16 kHz mono float PCM.
 */
#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>

#include <memory>

class QAudioSource;
class QIODevice;
class QTimer;

namespace kea {

class StreamingResampler;

class AudioRecorder : public QObject
{
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    /// Open the default input device. Virtual so unit tests can inject a
    /// FakeAudioRecorder that always succeeds without a real mic.
    virtual bool start();
    virtual void stop();
    virtual bool isActive() const { return m_source != nullptr; }
    QString lastError() const { return m_lastError; }

protected:
    void setLastError(const QString &e) { m_lastError = e; }

Q_SIGNALS:
    /// 16 kHz mono float samples (Qt6: QList is the container used across threads).
    void pcmBlock(const QList<float> &samples);
    void levelChanged(float level);

private:
    void poll();
    bool openInt16Mono(const QAudioDevice &input, int sampleRate);

    QAudioSource *m_source = nullptr;
    QPointer<QIODevice> m_io;
    QTimer *m_poll = nullptr;
    std::unique_ptr<StreamingResampler> m_resampler;
    QAudioFormat m_format;
    int m_bytesPerFrame = 0;
    QString m_lastError;
    bool m_stopping = false;
    QElapsedTimer m_levelThrottle;
};

} // namespace kea
