#pragma once

#include <QString>

#include "controllers/midi/midibytestream.h"
#include "controllers/midi/midicontroller.h"

// MIDI device whose byte stream arrives over a loopback TCP connection instead
// of a USB port, so a host can drive Mixxx as if a controller were plugged in.
// It is only created when [Controller],VirtualMidiPort is set, and it only
// accepts connections from 127.0.0.1.
class VirtualMidiController final : public MidiController {
  public:
    VirtualMidiController(const QString& name, quint16 port);
    ~VirtualMidiController() override;

  private:
    int open() override;
    int close() override;
    bool poll() override;
    bool isPolling() const override {
        return true;
    }
    void sendBytes(const QByteArray& bytes) override;
    void sendShortMsg(unsigned char status, unsigned char byte1, unsigned char byte2) override;

    void closeSockets();

    const quint16 m_port;
    int m_listenFd = -1;
    int m_clientFd = -1;
    mixxx::android::MidiByteStream m_parser;
};
