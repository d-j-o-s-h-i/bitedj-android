#pragma once

#include "util/assert.h"

#include <QUrl>

namespace mixxx {

class UrlResource {
  public:
    virtual ~UrlResource() = default;

    QUrl getUrl() const {
        return m_url;
    }
    QString getUrlString() const {
        return m_url.toString();
    }

  protected:
    explicit UrlResource(const QUrl& url)
            : m_url(url) {
    }

    inline bool isLocalFile() const {
        // TODO(XXX): We need more testing how network shares are
        // handled! From the documentation of QUrl::isLocalFile():
        // "Note that this function considers URLs with hostnames
        // to be local file paths, ..."
        return m_url.isLocalFile();
    }

    inline QString getLocalFileName() const {
#ifdef Q_OS_ANDROID
        if (m_url.scheme() == QLatin1String("content")) {
            return m_url.toString(QUrl::FullyEncoded);
        }
#endif
        DEBUG_ASSERT(isLocalFile());
        return m_url.toLocalFile();
    }

  private:
    QUrl m_url;
};

} // namespace mixxx
