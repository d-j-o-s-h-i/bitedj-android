#include "controllers/midi/androidmidienumerator.h"
#include <QJsonArray>
#include "controllers/midi/androidmidicontroller.h"
#include "controllers/midi/virtualmidicontroller.h"
#include "platform/android/androidplatform.h"

namespace {

// Host-driven stand-in for a real controller: the MIDI stream is fed over a
// loopback socket (see VirtualMidiController) so the mapping can be exercised
// without the hardware attached.
constexpr char kVirtualDeviceName[] = "Pioneer DDJ-FLX4";
constexpr char kVirtualMidiPortKey[] = "VirtualMidiPort";

} // namespace

AndroidMidiEnumerator::AndroidMidiEnumerator(UserSettingsPointer config)
        : m_pConfig(std::move(config)) {
}

AndroidMidiEnumerator::~AndroidMidiEnumerator() {
    qDeleteAll(m_devices);
}

QList<Controller*> AndroidMidiEnumerator::queryDevices() {
    // ControllerManager currently queries only at startup. Returning existing
    // instances avoids invalidating mappings and UI pointers on repeat queries.
    if (!m_devices.isEmpty()) return m_devices;
    for (const auto& value : mixxx::android::AndroidPlatform::snapshot().value("midiDevices").toArray()) {
        const auto device = value.toObject();
        if (device.value("type").toInt() != 1) continue; // USB, MIDI 1 byte stream
        const auto name = device.value("name").toString();
        m_devices.append(new AndroidMidiController(name, device.value("id").toInt(),
                device.value("outputPorts").toInt() > 0, device.value("inputPorts").toInt() > 0));
    }
    const int virtualPort = m_pConfig->getValue(
            ConfigKey(QStringLiteral("[Controller]"),
                    QString::fromLatin1(kVirtualMidiPortKey)),
            0);
    if (virtualPort > 0 && virtualPort <= 65535) {
        m_devices.append(new VirtualMidiController(
                QString::fromLatin1(kVirtualDeviceName), static_cast<quint16>(virtualPort)));
    }
    return m_devices;
}
