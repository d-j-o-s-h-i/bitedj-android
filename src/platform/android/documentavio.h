#pragma once

#include <cstdint>
#include <memory>
#include "documentinput.h"

struct AVIOContext;

namespace mixxx::android {

// FFmpeg custom I/O for a seekable original document. The logical position is
// private to this decoder; pread never changes a provider/shared fd's cursor.
class DocumentAvio final {
  public:
    explicit DocumentAvio(std::shared_ptr<DocumentInput> input);
    ~DocumentAvio();
    DocumentAvio(const DocumentAvio&) = delete;
    DocumentAvio& operator=(const DocumentAvio&) = delete;
    AVIOContext* context() const { return m_context; }
    int error() const { return m_error; }

  private:
    static int read(void* opaque, std::uint8_t* buffer, int size);
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence);
    std::shared_ptr<DocumentInput> m_input;
    AVIOContext* m_context = nullptr;
    std::int64_t m_position = 0;
    int m_error = 0;
};

} // namespace mixxx::android
