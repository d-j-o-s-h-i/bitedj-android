#pragma once

#include <QJniObject>
#include <amidi/AMidi.h>
#include "controllers/midi/midicontroller.h"
#include "controllers/midi/midibytestream.h"

class AndroidMidiController final : public MidiController {
  public:
    AndroidMidiController(const QString& name, int deviceId, bool receives, bool sends);
    ~AndroidMidiController() override;
  private:
    int open() override;
    int close() override;
    bool poll() override;
    bool isPolling() const override { return true; }
    void sendBytes(const QByteArray& bytes) override;
    void sendShortMsg(unsigned char status, unsigned char byte1, unsigned char byte2) override;
    void releasePorts();
    int m_deviceId;
    QJniObject m_javaDevice;
    AMidiDevice* m_device = nullptr;
    AMidiOutputPort* m_receive = nullptr; // device output -> Mixxx input
    AMidiInputPort* m_send = nullptr; // Mixxx output -> device input
    mixxx::android::MidiByteStream m_parser;
};
