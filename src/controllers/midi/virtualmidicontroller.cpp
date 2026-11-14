#include "controllers/midi/virtualmidicontroller.h"

#include <QDebug>
#include <QElapsedTimer>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <span>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::int64_t midiTimestampNanos() {
    static QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.nsecsElapsed();
}

} // namespace

VirtualMidiController::VirtualMidiController(const QString& name, quint16 port)
        : MidiController(name),
          m_port(port) {
    // Matches the category the Android USB devices report so the same
    // per-device mapping/auto-enable handling applies.
    setDeviceCategory(QStringLiteral("Android USB MIDI"));
    setInputDevice(true);
    setOutputDevice(true);
}

VirtualMidiController::~VirtualMidiController() {
    if (isOpen()) {
        close();
    } else {
        closeSockets();
    }
}

int VirtualMidiController::open() {
    if (isOpen()) {
        return -1;
    }
    closeSockets();
    const int listenFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd < 0) {
        qWarning() << "Virtual MIDI: socket() failed:" << std::strerror(errno);
        return -1;
    }
    int reuse = 1;
    ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(m_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(listenFd, 1) != 0) {
        qWarning() << "Virtual MIDI: cannot listen on 127.0.0.1:" << m_port
                   << std::strerror(errno);
        ::close(listenFd);
        return -1;
    }
    m_listenFd = listenFd;
    m_parser.reset();
    startEngine();
    if (!applyMapping()) {
        stopEngine();
        MidiController::close();
        closeSockets();
        return -3;
    }
    qInfo() << "Virtual MIDI: listening on 127.0.0.1:" << m_port;
    setOpen(true);
    return 0;
}

int VirtualMidiController::close() {
    if (!isOpen()) {
        return -1;
    }
    stopEngine();
    MidiController::close();
    closeSockets();
    setOpen(false);
    return 0;
}

void VirtualMidiController::closeSockets() {
    if (m_clientFd >= 0) {
        ::close(m_clientFd);
        m_clientFd = -1;
    }
    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
    m_parser.reset();
}

bool VirtualMidiController::poll() {
    if (m_listenFd < 0 || !isOpen()) {
        return false;
    }
    bool handled = false;
    // One host at a time: a new connection replaces the previous one, which
    // makes reconnecting after an adb forward restart work without a rescan.
    while (true) {
        const int clientFd = ::accept4(m_listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientFd < 0) {
            break;
        }
        if (m_clientFd >= 0) {
            ::close(m_clientFd);
        }
        int noDelay = 1;
        ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
        m_clientFd = clientFd;
        m_parser.reset();
        qInfo() << "Virtual MIDI: host connected";
    }
    if (m_clientFd < 0) {
        return false;
    }
    for (int round = 0; round < 8; ++round) {
        std::uint8_t buffer[1024];
        const auto count = ::recv(m_clientFd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (count > 0) {
            handled = true;
            m_parser.feed(std::span(buffer, static_cast<std::size_t>(count)),
                    midiTimestampNanos(),
                    [this](std::span<const std::uint8_t> message, std::int64_t nanos) {
                        const auto time = mixxx::Duration::fromNanos(nanos);
                        if (message[0] == 0xf0) {
                            receive(QByteArray(reinterpret_cast<const char*>(message.data()),
                                            static_cast<int>(message.size())),
                                    time);
                        } else {
                            receivedShortMessage(message[0],
                                    message.size() > 1 ? message[1] : 0,
                                    message.size() > 2 ? message[2] : 0,
                                    time);
                        }
                    });
            continue;
        }
        if (count == 0) {
            qInfo() << "Virtual MIDI: host disconnected";
            ::close(m_clientFd);
            m_clientFd = -1;
            m_parser.reset();
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            qWarning() << "Virtual MIDI: receive failed:" << std::strerror(errno);
            ::close(m_clientFd);
            m_clientFd = -1;
            m_parser.reset();
        }
        break;
    }
    return handled;
}

void VirtualMidiController::sendBytes(const QByteArray& bytes) {
    if (m_clientFd < 0 || bytes.isEmpty()) {
        return;
    }
    const auto written = ::send(m_clientFd, bytes.constData(), bytes.size(),
            MSG_DONTWAIT | MSG_NOSIGNAL);
    if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        qWarning() << "Virtual MIDI: send failed:" << std::strerror(errno);
    }
}

void VirtualMidiController::sendShortMsg(
        unsigned char status, unsigned char byte1, unsigned char byte2) {
    const char bytes[] = {static_cast<char>(status), static_cast<char>(byte1),
            static_cast<char>(byte2)};
    const int size = mixxx::android::MidiByteStream::messageSize(status);
    if (size > 0) {
        sendBytes(QByteArray(bytes, size));
    }
}
