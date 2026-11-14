#pragma once

#include <QObject>
#include <QTimer>
#include <atomic>
#include <memory>
#include <oboe/Oboe.h>

#include "soundio/sounddevice.h"
#include "util/performancetimer.h"

// One Android output stream drives the Mixxx engine clock. Master and cue use
// distinct channel pairs in the same stream; secondary hardware clocks and
// recording inputs are deliberately rejected until their synchronization exists.
class SoundDeviceAndroid final : public QObject, public SoundDevice,
                                 public oboe::AudioStreamDataCallback,
                                 public oboe::AudioStreamErrorCallback {
  public:
    SoundDeviceAndroid(UserSettingsPointer config, SoundManager* manager,
            int deviceId, const QString& name, const QString& key, int channels,
            mixxx::audio::SampleRate defaultRate);
    ~SoundDeviceAndroid() override;
    SoundDeviceStatus open(bool isClockReference, int syncBuffers) override;
    SoundDeviceStatus close() override;
    bool isOpen() const override;
    void readProcess(SINT) override {}
    void writeProcess(SINT) override {}
    QString getError() const override;
    mixxx::audio::SampleRate getDefaultSampleRate() const override;
    bool isStreamHealthy() const override;
    quint64 callbackTick() const override {
        return m_callbackTick.load(std::memory_order_relaxed);
    }
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream*, void*, int32_t) override;
    bool onError(oboe::AudioStream*, oboe::Result) override;

  private:
    std::shared_ptr<oboe::AudioStream> m_stream;
    mixxx::audio::SampleRate m_defaultRate;
    QString m_error;
    QTimer m_monitor;
    PerformanceTimer m_clock;
    std::atomic<bool> m_running{false};
    std::atomic<oboe::Result> m_streamError{oboe::Result::OK};
    std::atomic<double> m_latencySeconds{0.0};
    std::atomic<quint64> m_callbackTick{0};
    int m_channels = 0;
    int m_lastXrunCount = 0;
};
