#include "controllers/midi/androidmidicontroller.h"
#include <QCoreApplication>
#include <QJniEnvironment>

AndroidMidiController::AndroidMidiController(const QString& name, int deviceId,
        bool receives, bool sends) : MidiController(name), m_deviceId(deviceId) {
    setDeviceCategory(QStringLiteral("Android USB MIDI"));
    setInputDevice(receives);
    setOutputDevice(sends);
}
AndroidMidiController::~AndroidMidiController() {
    if (isOpen()) close();
    else releasePorts();
}
int AndroidMidiController::open() {
    if (isOpen()) return -1;
    const auto context = QNativeInterface::QAndroidApplication::context();
    m_javaDevice = QJniObject::callStaticObjectMethod("io/github/djoshi/bitedj/MidiBridge", "openDevice",
            "(Landroid/content/Context;I)Landroid/media/midi/MidiDevice;", context.object(), m_deviceId);
    if (!m_javaDevice.isValid()) return -1;
    QJniEnvironment environment;
    if (AMidiDevice_fromJava(environment.jniEnv(), m_javaDevice.object(), &m_device) != AMEDIA_OK ||
            (isInputDevice() && AMidiOutputPort_open(m_device, 0, &m_receive) != AMEDIA_OK) ||
            (isOutputDevice() && AMidiInputPort_open(m_device, 0, &m_send) != AMEDIA_OK)) {
        releasePorts();
        return -2;
    }
    m_parser.reset();
    startEngine();
    if (!applyMapping()) {
        stopEngine();
        MidiController::close();
        releasePorts();
        return -3;
    }
    setOpen(true);
    return 0;
}
int AndroidMidiController::close() {
    if (!isOpen()) return -1;
    stopEngine();
    MidiController::close();
    releasePorts();
    setOpen(false);
    return 0;
}
void AndroidMidiController::releasePorts() {
    if (m_receive) { AMidiOutputPort_close(m_receive); m_receive = nullptr; }
    if (m_send) { AMidiInputPort_close(m_send); m_send = nullptr; }
    if (m_device) { AMidiDevice_release(m_device); m_device = nullptr; }
    if (m_javaDevice.isValid()) {
        QJniObject::callStaticMethod<void>("io/github/djoshi/bitedj/MidiBridge", "closeDevice",
                "(Landroid/media/midi/MidiDevice;)V", m_javaDevice.object());
        m_javaDevice = {};
    }
    m_parser.reset();
}
bool AndroidMidiController::poll() {
    if (!m_receive || !isOpen()) return false;
    bool handled = false;
    for (int packet = 0; packet < 1024; ++packet) {
        std::uint8_t buffer[1024];
        int32_t opcode = 0;
        size_t count = 0;
        int64_t timestamp = 0;
        const auto result = AMidiOutputPort_receive(m_receive, &opcode,
                buffer, sizeof(buffer), &count, &timestamp);
        if (!result) break;
        if (result < 0) { close(); break; }
        if (opcode == AMIDI_OPCODE_FLUSH) { m_parser.reset(); continue; }
        if (opcode != AMIDI_OPCODE_DATA || !count) continue;
        handled = true;
        m_parser.feed(std::span(buffer, count), timestamp,
                [this](std::span<const std::uint8_t> message, std::int64_t nanos) {
                    const auto time = mixxx::Duration::fromNanos(nanos);
                    if (message[0] == 0xf0) {
                        receive(QByteArray(reinterpret_cast<const char*>(message.data()), message.size()), time);
                    } else {
                        receivedShortMessage(message[0], message.size() > 1 ? message[1] : 0,
                                message.size() > 2 ? message[2] : 0, time);
                    }
                });
    }
    return handled;
}
void AndroidMidiController::sendBytes(const QByteArray& bytes) {
    if (!m_send || bytes.isEmpty()) return;
    const auto result = AMidiInputPort_send(m_send,
            reinterpret_cast<const std::uint8_t*>(bytes.constData()), bytes.size());
    if (result != bytes.size()) qCWarning(m_logOutput) << "Android MIDI send incomplete:" << result;
}
void AndroidMidiController::sendShortMsg(unsigned char status, unsigned char byte1, unsigned char byte2) {
    const char bytes[] = {char(status), char(byte1), char(byte2)};
    const int size = mixxx::android::MidiByteStream::messageSize(status);
    if (size > 0) sendBytes(QByteArray(bytes, size));
}
