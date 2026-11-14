#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace mixxx::android {

// MIDI byte streams may split messages, use running status, and interleave
// realtime bytes inside SysEx. Bounded storage prevents an invalid controller
// from growing the receive queue indefinitely. No allocation occurs here.
class MidiByteStream final {
  public:
    static constexpr std::size_t kMaxSysex = 4096;
    static int messageSize(std::uint8_t status) {
        if (status < 0x80) return 0;
        if (status < 0xf0) return (status & 0xe0) == 0xc0 ? 2 : 3;
        switch (status) {
        case 0xf1: case 0xf3: return 2;
        case 0xf2: return 3;
        case 0xf6: return 1;
        default: return status >= 0xf8 ? 1 : 0;
        }
    }
    void reset() {
        m_status = 0;
        m_size = 0;
        m_sysex = false;
        m_overflow = false;
    }
    template<class Dispatch>
    void feed(std::span<const std::uint8_t> bytes, std::int64_t timestamp, Dispatch dispatch) {
        for (auto byte : bytes) {
            if (byte >= 0xf8) {
                dispatch(std::span<const std::uint8_t>(&byte, 1), timestamp);
                continue;
            }
            if (m_sysex) {
                if (byte < 0x80 || byte == 0xf7) {
                    if (!m_overflow) {
                        if (m_size == m_data.size()) m_overflow = true;
                        else m_data[m_size++] = byte;
                    }
                    if (byte == 0xf7) {
                        if (!m_overflow) dispatch(std::span(m_data.data(), m_size), m_timestamp);
                        reset();
                    }
                    continue;
                }
                reset(); // status interrupted SysEx; resynchronize at that status
            }
            if (byte >= 0x80) {
                m_size = 0;
                m_status = byte < 0xf0 ? byte : 0;
                m_timestamp = timestamp;
                if (byte == 0xf0) {
                    m_sysex = true;
                    m_data[m_size++] = byte;
                } else if (const auto size = messageSize(byte); size > 0) {
                    m_data[m_size++] = byte;
                    if (size == 1) {
                        dispatch(std::span(m_data.data(), m_size), timestamp);
                        m_size = 0;
                    }
                }
                continue;
            }
            if (!m_size) {
                if (!m_status) continue;
                m_timestamp = timestamp;
                m_data[m_size++] = m_status;
            }
            m_data[m_size++] = byte;
            if (m_size == static_cast<std::size_t>(messageSize(m_data[0]))) {
                dispatch(std::span(m_data.data(), m_size), m_timestamp);
                m_size = 0;
            }
        }
    }

  private:
    std::array<std::uint8_t, kMaxSysex> m_data{};
    std::size_t m_size = 0;
    std::uint8_t m_status = 0;
    std::int64_t m_timestamp = 0;
    bool m_sysex = false;
    bool m_overflow = false;
};
} // namespace mixxx::android
