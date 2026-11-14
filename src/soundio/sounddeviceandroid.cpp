#include "soundio/sounddeviceandroid.h"

#include <algorithm>

#include "control/controlobject.h"
#include "waveform/visualplayposition.h"
#include "soundio/soundmanager.h"
#include "util/defs.h"

SoundDeviceAndroid::SoundDeviceAndroid(UserSettingsPointer config, SoundManager* manager,
        int deviceId, const QString& name, const QString& key, int channels,
        mixxx::audio::SampleRate defaultRate)
        : SoundDevice(config, manager), m_defaultRate(defaultRate) {
    m_deviceId.name = name;
    m_deviceId.backend = QStringLiteral("android-oboe");
    m_deviceId.persistentKey = key;
    m_deviceId.androidDeviceId = deviceId;
    m_strDisplayName = name;
    m_numOutputChannels = mixxx::audio::ChannelCount::fromInt(channels);
    m_numInputChannels = mixxx::audio::ChannelCount();
    m_hostAPI = QStringLiteral("Android (Oboe)");
    connect(&m_monitor, &QTimer::timeout, this, [this] {
        const auto error = m_streamError.load(std::memory_order_acquire);
        if (error != oboe::Result::OK) {
            m_error = QString::fromUtf8(oboe::convertToText(error));
            // Never reopen onto the speaker after losing the selected USB route.
            m_pSoundManager->stopDevices();
            return;
        }
        if (!m_stream) return;
        if (m_deviceId.androidDeviceId != 0 &&
                m_stream->getDeviceId() != m_deviceId.androidDeviceId) {
            m_streamError.store(oboe::Result::ErrorDisconnected, std::memory_order_release);
            m_running.store(false, std::memory_order_release);
            m_error = QStringLiteral("Selected Android output route disconnected");
            m_pSoundManager->stopDevices();
            return;
        }
        const auto latency = m_stream->calculateLatencyMillis();
        if (latency && latency.value() >= 0) {
            m_latencySeconds.store(latency.value() / 1000.0, std::memory_order_relaxed);
        }
        const auto xruns = m_stream->getXRunCount();
        if (xruns && xruns.value() > m_lastXrunCount) {
            m_lastXrunCount = xruns.value();
            m_pSoundManager->underflowHappened(6);
        }
    });
}

SoundDeviceAndroid::~SoundDeviceAndroid() { close(); }

SoundDeviceStatus SoundDeviceAndroid::open(bool isClockReference, int syncBuffers) {
    close();
    m_error.clear();
    if (!isClockReference || !m_audioInputs.isEmpty()) {
        m_error = QStringLiteral("Android currently requires one output clock and no recording inputs");
        return SoundDeviceStatus::Error;
    }
    m_channels = 0;
    for (const auto& output : m_audioOutputs) {
        const auto group = output.getChannelGroup();
        m_channels = std::max<int>(m_channels,
                group.getChannelBase() + group.getChannelCount().value());
    }
    if (m_channels <= 0 || m_channels > m_numOutputChannels.value()) {
        m_error = QStringLiteral("Invalid Android output channel selection");
        return SoundDeviceStatus::Error;
    }
    oboe::AudioStreamBuilder builder;
    builder.setAudioApi(oboe::AudioApi::AAudio)
            ->setDirection(oboe::Direction::Output)
            ->setDeviceId(m_deviceId.androidDeviceId)
            ->setChannelCount(m_channels)
            ->setSampleRate(m_sampleRate.value())
            ->setFormat(oboe::AudioFormat::Float)
            ->setFormatConversionAllowed(false)
            ->setChannelConversionAllowed(false)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setUsage(oboe::Usage::Media)
            ->setContentType(oboe::ContentType::Music)
            ->setDataCallback(this)
            ->setErrorCallback(this);
    auto result = builder.openStream(m_stream);
    if (result != oboe::Result::OK) {
        builder.setSharingMode(oboe::SharingMode::Shared);
        result = builder.openStream(m_stream);
    }
    if (result != oboe::Result::OK) {
        m_error = QString::fromUtf8(oboe::convertToText(result));
        return SoundDeviceStatus::Error;
    }
    if (m_stream->getChannelCount() != m_channels ||
            m_stream->getFormat() != oboe::AudioFormat::Float ||
            m_stream->getSampleRate() != static_cast<int32_t>(m_sampleRate.value()) ||
            (m_deviceId.androidDeviceId != 0 &&
                    m_stream->getDeviceId() != m_deviceId.androidDeviceId)) {
        m_error = QStringLiteral("Android did not provide the requested route, channels, format or rate");
        close();
        return SoundDeviceStatus::Error;
    }
    const int bufferFrames = std::max<int>(m_configFramesPerBuffer,
            m_stream->getFramesPerBurst() * std::max(2, syncBuffers));
    m_stream->setBufferSizeInFrames(bufferFrames);
    m_latencySeconds.store(static_cast<double>(m_stream->getBufferSizeInFrames()) /
            m_stream->getSampleRate(), std::memory_order_relaxed);
    m_lastXrunCount = 0;
    m_streamError.store(oboe::Result::OK, std::memory_order_release);
    m_clock.start();
    m_running.store(true, std::memory_order_release);
    result = m_stream->requestStart();
    if (result != oboe::Result::OK) {
        m_error = QString::fromUtf8(oboe::convertToText(result));
        close();
        return SoundDeviceStatus::Error;
    }
    m_monitor.start(100);
    return SoundDeviceStatus::Ok;
}

SoundDeviceStatus SoundDeviceAndroid::close() {
    m_monitor.stop();
    m_running.store(false, std::memory_order_release);
    if (m_stream) {
        m_stream->requestStop();
        m_stream->close(); // waits for in-flight callbacks before buffer teardown
        m_stream.reset();
    }
    return SoundDeviceStatus::Ok;
}

bool SoundDeviceAndroid::isOpen() const { return bool(m_stream); }
QString SoundDeviceAndroid::getError() const { return m_error; }
mixxx::audio::SampleRate SoundDeviceAndroid::getDefaultSampleRate() const { return m_defaultRate; }

bool SoundDeviceAndroid::isStreamHealthy() const {
    // Only an explicitly reported stream error means the route is gone; the
    // absence of a stream means we are not open, which the watchdog ignores.
    return !m_stream ||
            m_streamError.load(std::memory_order_acquire) == oboe::Result::OK;
}

oboe::DataCallbackResult SoundDeviceAndroid::onAudioReady(
        oboe::AudioStream*, void* buffer, int32_t frames) {
    // Bumped on entry, before any processing, so the device watchdog sees a
    // tick even if the engine itself stalls inside the callback.
    m_callbackTick.fetch_add(1, std::memory_order_relaxed);
    auto* samples = static_cast<CSAMPLE*>(buffer);
    std::fill_n(samples, frames * m_channels, CSAMPLE(0));
    if (!m_running.load(std::memory_order_acquire)) return oboe::DataCallbackResult::Stop;
    for (int offset = 0; offset < frames;) {
        const int chunk = std::min<int>(frames - offset, kMaxEngineFrames);
        m_clock.restart();
        VisualPlayPosition::setCallbackEntryToDacSecs(
                m_latencySeconds.load(std::memory_order_relaxed) +
                        static_cast<double>(offset) / m_sampleRate.value(), m_clock);
        m_pSoundManager->processUnderflowHappened(chunk);
        m_pSoundManager->readProcess(chunk);
        m_pSoundManager->onDeviceOutputCallback(chunk);
        composeOutputBuffer(samples + offset * m_channels, chunk, 0, m_channels);
        m_pSoundManager->writeProcess(chunk);
        offset += chunk;
    }
    return oboe::DataCallbackResult::Continue;
}

bool SoundDeviceAndroid::onError(oboe::AudioStream*, oboe::Result error) {
    m_running.store(false, std::memory_order_release);
    m_streamError.store(error, std::memory_order_release);
    // Management thread owns stop/close; Oboe must not close concurrently.
    return true;
}
