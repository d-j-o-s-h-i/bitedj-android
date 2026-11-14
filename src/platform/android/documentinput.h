#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace mixxx::android {

// Owns a read-only, seekable descriptor transferred from ParcelFileDescriptor.
// Offsets are relative to the document, including for AssetFileDescriptor slices.
// pread keeps independent readers from changing the shared kernel file position.
class DocumentInput final {
  public:
    DocumentInput(int ownedFd, std::int64_t startOffset, std::int64_t length = -1);
    ~DocumentInput();
    DocumentInput(const DocumentInput&) = delete;
    DocumentInput& operator=(const DocumentInput&) = delete;
    DocumentInput(DocumentInput&&) = delete;
    DocumentInput& operator=(DocumentInput&&) = delete;

    bool isValid() const { return m_error == 0; }
    int error() const { return m_error; }
    std::int64_t size() const { return m_length; }
    ssize_t readAt(std::int64_t offset, void* buffer, std::size_t bytes) const;

  private:
    int m_fd;
    int m_error = 0;
    std::int64_t m_start;
    std::int64_t m_length;
};

} // namespace mixxx::android
