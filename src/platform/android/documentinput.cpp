#include "documentinput.h"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace mixxx::android {

DocumentInput::DocumentInput(int ownedFd, std::int64_t startOffset, std::int64_t length)
        : m_fd(ownedFd), m_start(startOffset), m_length(length) {
    static_assert(sizeof(off_t) >= sizeof(std::int64_t));
    if (m_fd < 0 || m_start < 0 || m_length < -1) {
        m_error = EINVAL;
        return;
    }
    if (lseek(m_fd, 0, SEEK_CUR) == -1) {
        m_error = errno;
        return;
    }
    struct stat info {};
    if (fstat(m_fd, &info) == -1) {
        m_error = errno;
        return;
    }
    if (m_length == -1) {
        // A provider can return a seekable proxy descriptor whose st_size is 0.
        const auto end = S_ISREG(info.st_mode) && info.st_size > 0
                ? info.st_size : lseek(m_fd, 0, SEEK_END);
        if (end < m_start) {
            m_error = end == -1 ? errno : EINVAL;
            return;
        }
        m_length = end - m_start;
    }
    if (m_length > std::numeric_limits<std::int64_t>::max() - m_start) {
        m_error = EOVERFLOW;
        return;
    }
    if (S_ISREG(info.st_mode) && info.st_size > 0 &&
            m_start + m_length > info.st_size) {
        m_error = EINVAL;
    }
}

DocumentInput::~DocumentInput() {
    if (m_fd >= 0) {
        // Do not retry close after EINTR: the fd may already have been released.
        close(m_fd);
    }
}

ssize_t DocumentInput::readAt(std::int64_t offset, void* buffer, std::size_t bytes) const {
    if (!isValid() || offset < 0 || offset > m_length || (!buffer && bytes)) {
        errno = isValid() ? EINVAL : m_error;
        return -1;
    }
    const auto available = static_cast<std::uint64_t>(m_length - offset);
    const auto count = std::min<std::uint64_t>(
            std::min<std::uint64_t>(bytes, available),
            std::numeric_limits<ssize_t>::max());
    if (count == 0) {
        return 0;
    }
    ssize_t result;
    do {
        result = pread(m_fd, buffer, count, m_start + offset);
    } while (result == -1 && errno == EINTR);
    return result;
}

} // namespace mixxx::android
