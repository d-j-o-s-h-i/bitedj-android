#include "documentavio.h"

#include <cerrno>
#include <cstdio>
#include <limits>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace mixxx::android {

DocumentAvio::DocumentAvio(std::shared_ptr<DocumentInput> input) : m_input(std::move(input)) {
    if (!m_input || !m_input->isValid()) {
        m_error = AVERROR(m_input ? m_input->error() : EINVAL);
        return;
    }
    constexpr int bufferSize = 32768;
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(bufferSize));
    if (!buffer) {
        m_error = AVERROR(ENOMEM);
        return;
    }
    m_context = avio_alloc_context(buffer, bufferSize, 0, this, &read, nullptr, &seek);
    if (!m_context) {
        av_free(buffer);
        m_error = AVERROR(ENOMEM);
        return;
    }
    m_context->seekable = AVIO_SEEKABLE_NORMAL;
}

DocumentAvio::~DocumentAvio() {
    if (m_context) {
        // FFmpeg can replace the initial buffer; free the current buffer.
        av_freep(&m_context->buffer);
        avio_context_free(&m_context);
    }
}

int DocumentAvio::read(void* opaque, std::uint8_t* buffer, int size) {
    auto* self = static_cast<DocumentAvio*>(opaque);
    if (size <= 0) return AVERROR(EINVAL);
    const auto count = self->m_input->readAt(self->m_position, buffer, size);
    if (count < 0) return AVERROR(errno);
    if (!count) return AVERROR_EOF;
    self->m_position += count;
    return static_cast<int>(count);
}

std::int64_t DocumentAvio::seek(void* opaque, std::int64_t offset, int whence) {
    auto* self = static_cast<DocumentAvio*>(opaque);
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE) return self->m_input->size();
    std::int64_t base = 0;
    switch (whence) {
    case SEEK_SET: break;
    case SEEK_CUR: base = self->m_position; break;
    case SEEK_END: base = self->m_input->size(); break;
    default: return AVERROR(EINVAL);
    }
    if ((offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset) ||
            (offset < 0 && base < std::numeric_limits<std::int64_t>::min() - offset)) {
        return AVERROR(EOVERFLOW);
    }
    const auto position = base + offset;
    if (position < 0 || position > self->m_input->size()) return AVERROR(EINVAL);
    self->m_position = position;
    return position;
}

} // namespace mixxx::android
