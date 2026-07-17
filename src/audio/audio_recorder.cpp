/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "audio_recorder.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>

#include "resampler.h"

namespace kea {

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent)
{
}

AudioRecorder::~AudioRecorder()
{
    stop();
}

bool AudioRecorder::start()
{
    const QAudioDevice input = QMediaDevices::defaultAudioInput();
    if (input.isNull()) {
        return false; // no microphone available
    }

    QAudioFormat format;
    format.setSampleRate(m_inRate);   // PipeWire default ~48 kHz
    format.setChannelCount(1);        // mono
    format.setSampleFormat(QAudioFormat::Int16);

    if (!input.isFormatSupported(format)) {
        format = input.preferredFormat();
        m_inRate = format.sampleRate();
        // Resampler adapts to whatever rate we actually get.
    }
    if (format.channelCount() < 1) {
        return false;
    }

    m_resampler = std::make_unique<StreamingResampler>(m_inRate, 16000);
    m_source = new QAudioSource(input, format, this);
    m_source->setBufferSize(m_inRate / 10); // ~100 ms buffer
    m_io = m_source->start();
    if (!m_io) {
        delete m_source;
        m_source = nullptr;
        return false;
    }
    connect(m_io, &QIODevice::readyRead, this, &AudioRecorder::onReadyRead);
    return true;
}

void AudioRecorder::stop()
{
    if (m_source) {
        m_source->stop();
        delete m_source;
        m_source = nullptr;
        m_io = nullptr;
    }
    if (m_resampler) {
        // Flush any residual samples held in the resampler.
        const auto tail = m_resampler->flush();
        if (!tail.empty()) {
            QVector<float> q(tail.begin(), tail.end());
            Q_EMIT pcmBlock(q);
        }
        m_resampler.reset();
    }
}

void AudioRecorder::onReadyRead()
{
    if (!m_io || !m_resampler) {
        return;
    }
    const qint64 available = m_io->bytesAvailable();
    if (available <= 0) {
        return;
    }
    const QByteArray raw = m_io->read(available);
    const int n = raw.size() / static_cast<int>(sizeof(int16_t));
    if (n <= 0) {
        return;
    }
    const auto *samples = reinterpret_cast<const int16_t *>(raw.constData());

    // RMS over the raw int16 block for the UI meter.
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double v = samples[i] / 32768.0;
        sumSq += v * v;
    }
    const float rms = static_cast<float>(std::sqrt(sumSq / n));
    Q_EMIT levelChanged(rms);

    // Resample to 16 kHz mono float and forward.
    const auto out = m_resampler->push(samples, static_cast<std::size_t>(n));
    if (!out.empty()) {
        QVector<float> q(out.begin(), out.end());
        Q_EMIT pcmBlock(q);
    }
}

} // namespace kea
