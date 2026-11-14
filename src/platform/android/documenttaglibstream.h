#pragma once

#include <algorithm>
#include <limits>
#include <memory>
#include <tiostream.h>

#include "platform/android/documentinput.h"

namespace mixxx::android {

// TagLib borrows this read-only stream; it must outlive the tag file.
class DocumentTaglibStream final : public TagLib::IOStream {
  public:
    explicit DocumentTaglibStream(std::shared_ptr<DocumentInput> input)
            : m_input(std::move(input)) {
    }
    TagLib::FileName name() const override { return ""; }
    bool readOnly() const override { return true; }
    bool isOpen() const override {
        return m_input && m_input->isValid() &&
                m_input->size() <= std::numeric_limits<long>::max();
    }
    TagLib::ByteVector readBlock(unsigned long count) override {
        if (!isOpen()) return {};
        count = std::min<unsigned long>(count, m_input->size() - m_position);
        count = std::min<unsigned long>(count, std::numeric_limits<unsigned int>::max());
        TagLib::ByteVector bytes(static_cast<unsigned int>(count));
        const auto read = m_input->readAt(m_position, bytes.data(), count);
        if (read < 0) return {};
        bytes.resize(static_cast<unsigned int>(read));
        m_position += read;
        return bytes;
    }
    void seek(long offset, Position origin = Beginning) override {
        if (!isOpen()) return;
        const auto base = origin == Current ? m_position : origin == End ? length() : 0;
        if ((offset > 0 && base > std::numeric_limits<long>::max() - offset) ||
                (offset < 0 && base < std::numeric_limits<long>::min() - offset)) return;
        const auto position = base + offset;
        if (position >= 0 && position <= length()) m_position = position;
    }
    long tell() const override { return m_position; }
    long length() override { return isOpen() ? m_input->size() : 0; }
    void writeBlock(const TagLib::ByteVector&) override {}
    void insert(const TagLib::ByteVector&, unsigned long, unsigned long) override {}
    void removeBlock(unsigned long, unsigned long) override {}
    void truncate(long) override {}

  private:
    std::shared_ptr<DocumentInput> m_input;
    long m_position = 0;
};

} // namespace mixxx::android
