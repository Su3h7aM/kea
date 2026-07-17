/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "audio_recorder.h"

#include <QAudio>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <vector>

#include "logging.h"
#include "resampler.h"

namespace kea {

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent)
    , m_poll(new QTimer(this))
{
    m_poll->setInterval(20); // 50 Hz pull — avoids readyRead re-entrancy in PW/FFmpeg
    connect(m_poll, &QTimer::timeout, this, &AudioRecorder::poll);
}

AudioRecorder::~AudioRecorder()
{
    stop();
}

bool AudioRecorder::openInt16Mono(const QAudioDevice &input, int sampleRate)
{
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    auto *source = new QAudioSource(input, format);
    const int bpf = format.bytesPerFrame(); // 2 for int16 mono
    if (bpf > 0) {
        // ~200 ms buffer in bytes
        source->setBufferSize(sampleRate * bpf / 5);
    }

    QIODevice *io = source->start();
    if (!io || source->error() != QAudio::NoError || source->state() == QAudio::StoppedState) {
        m_lastError = QStringLiteral("QAudioSource start failed (error %1 state %2)")
                          .arg(int(source->error()))
                          .arg(int(source->state()));
        source->stop();
        delete source;
        return false;
    }

    m_format = format;
    m_bytesPerFrame = bpf > 0 ? bpf : 2;
    m_source = source;
    m_io = io;
    m_resampler = std::make_unique<StreamingResampler>(sampleRate, 16000);

    // Parent only after success so failed attempts don't leave half-live children.
    source->setParent(this);

    qCInfo(keaLog) << "mic open:" << input.description()
                    << "rate" << sampleRate << "ch 1 fmt Int16";
    return true;
}

bool AudioRecorder::start()
{
    if (m_source) {
        return true;
    }
    m_stopping = false;
    m_lastError.clear();

    const QAudioDevice input = QMediaDevices::defaultAudioInput();
    if (input.isNull()) {
        m_lastError = QStringLiteral("no default audio input device");
        qCWarning(keaLog) << m_lastError;
        return false;
    }

    // Single simple format path — no multi-format probe (that hammers PipeWire SPA).
    const int rates[] = {48000, 44100, 16000};
    for (int rate : rates) {
        if (openInt16Mono(input, rate)) {
            m_poll->start();
            return true;
        }
    }

    qCWarning(keaLog) << "mic open failed:" << m_lastError << "device" << input.description();
    return false;
}

void AudioRecorder::stop()
{
    m_stopping = true;
    if (m_poll) {
        m_poll->stop();
    }

    // Drop the I/O pointer first so poll() no-ops even if a timer event is pending.
    m_io.clear();

    QList<float> tailPcm;
    if (m_resampler) {
        const auto tail = m_resampler->flush();
        m_resampler.reset();
        if (!tail.empty()) {
            tailPcm.reserve(int(tail.size()));
            for (float s : tail) {
                tailPcm.push_back(s);
            }
        }
    }
    m_bytesPerFrame = 0;

    if (m_source) {
        m_source->stop();
        // Defer destruction so any in-flight multimedia callbacks finish.
        m_source->deleteLater();
        m_source = nullptr;
    }

    m_stopping = false;

    // Emit residual PCM after the device is fully stopped (avoids re-entrancy).
    if (!tailPcm.isEmpty()) {
        Q_EMIT pcmBlock(tailPcm);
    }
}

void AudioRecorder::poll()
{
    if (m_stopping || !m_source || m_io.isNull() || !m_resampler || m_bytesPerFrame <= 0) {
        return;
    }
    if (m_source->state() != QAudio::ActiveState && m_source->state() != QAudio::IdleState) {
        return;
    }

    const qint64 available = m_io->bytesAvailable();
    if (available < m_bytesPerFrame) {
        return;
    }

    const qint64 nFrames = available / m_bytesPerFrame;
    // Cap read size to avoid huge spikes (~100 ms).
    const qint64 maxFrames = m_format.sampleRate() / 10;
    const qint64 frames = std::min(nFrames, maxFrames);
    const qint64 nBytes = frames * m_bytesPerFrame;

    const QByteArray raw = m_io->read(nBytes);
    if (raw.size() < m_bytesPerFrame) {
        return;
    }

    const int got = raw.size() / m_bytesPerFrame;
    const auto *samples = reinterpret_cast<const int16_t *>(raw.constData());

    // Throttle level meter to ~10 Hz so QML/Vulkan isn't flooded.
    if (!m_levelThrottle.isValid() || m_levelThrottle.elapsed() >= 100) {
        double sumSq = 0.0;
        for (int i = 0; i < got; ++i) {
            const double v = samples[i] / 32768.0;
            sumSq += v * v;
        }
        Q_EMIT levelChanged(got > 0 ? float(std::sqrt(sumSq / got)) : 0.0f);
        m_levelThrottle.restart();
    }

    // Resample int16 → 16 kHz float via existing path.
    const auto out = m_resampler->push(samples, static_cast<std::size_t>(got));
    if (!out.empty()) {
        QList<float> q;
        q.reserve(int(out.size()));
        for (float s : out) {
            q.push_back(s);
        }
        Q_EMIT pcmBlock(q);
    }
}

} // namespace kea
