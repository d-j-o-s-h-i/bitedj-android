#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include "documentinput.h"

namespace mixxx::android {

struct DocumentResult {
    QJsonObject metadata;
    std::shared_ptr<DocumentInput> input;
    QString error;
};

class AndroidPlatform final : public QObject {
    Q_OBJECT
  public:
    explicit AndroidPlatform(QObject* parent = nullptr);
    ~AndroidPlatform() override;
    static QString settingsPath();
    static QString diagnosticsPath();
    static QString resourcesPath();
    static QStringList storagePaths();
    static void finishActivity();
    static QJsonObject snapshot();
    static std::shared_ptr<DocumentInput> openDocument(const QString& uri);
    void chooseMusicFolder();
    void probeSelectedFolder();

  signals:
    void documentOpened(const mixxx::android::DocumentResult& result);
};

} // namespace mixxx::android
