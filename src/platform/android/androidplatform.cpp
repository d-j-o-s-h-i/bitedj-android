#include "androidplatform.h"

#include <QCoreApplication>
#include <QJniObject>
#include <QJniEnvironment>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaObject>
#include <mutex>
#include "moc_androidplatform.cpp"

namespace {
constexpr const char* kBridge = "io/github/djoshi/bitedj/PlatformBridge";
std::mutex s_receiverMutex;
mixxx::android::AndroidPlatform* s_receiver = nullptr;
}

namespace mixxx::android {

AndroidPlatform::AndroidPlatform(QObject* parent) : QObject(parent) {
    std::lock_guard lock(s_receiverMutex);
    Q_ASSERT(!s_receiver);
    s_receiver = this;
}

AndroidPlatform::~AndroidPlatform() {
    std::lock_guard lock(s_receiverMutex);
    s_receiver = nullptr;
}

QString AndroidPlatform::settingsPath() {
    const auto context = QNativeInterface::QAndroidApplication::context();
    return QJniObject::callStaticObjectMethod(kBridge, "settingsPath",
            "(Landroid/content/Context;)Ljava/lang/String;", context.object()).toString();
}

QJsonObject AndroidPlatform::snapshot() {
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto json = QJniObject::callStaticObjectMethod(kBridge, "snapshot",
            "(Landroid/content/Context;)Ljava/lang/String;", context.object()).toString();
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

std::shared_ptr<DocumentInput> AndroidPlatform::openDocument(const QString& uri) {
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto descriptor = QJniObject::callStaticObjectMethod(kBridge, "openDocument",
            "(Landroid/content/Context;Ljava/lang/String;)[J", context.object(),
            QJniObject::fromString(uri).object<jstring>());
    if (!descriptor.isValid()) {
        return {};
    }
    QJniEnvironment environment;
    jlong values[3];
    environment->GetLongArrayRegion(descriptor.object<jlongArray>(), 0, 3, values);
    return std::make_shared<DocumentInput>(values[0], values[1], values[2]);
}

QString AndroidPlatform::diagnosticsPath() {
    const auto context = QNativeInterface::QAndroidApplication::context();
    return QJniObject::callStaticObjectMethod(kBridge, "diagnosticsPath",
            "(Landroid/content/Context;)Ljava/lang/String;", context.object()).toString();
}

QString AndroidPlatform::resourcesPath() {
    const auto context = QNativeInterface::QAndroidApplication::context();
    return QJniObject::callStaticObjectMethod(kBridge, "resourcesPath",
            "(Landroid/content/Context;)Ljava/lang/String;", context.object()).toString();
}

QStringList AndroidPlatform::storagePaths() {
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto json = QJniObject::callStaticObjectMethod(kBridge, "storagePaths",
            "(Landroid/content/Context;)Ljava/lang/String;", context.object()).toString();
    QStringList paths;
    for (const auto& value : QJsonDocument::fromJson(json.toUtf8()).array()) {
        paths.append(value.toString());
    }
    return paths;
}

void AndroidPlatform::chooseMusicFolder() {
    QNativeInterface::QAndroidApplication::context().callMethod<void>("selectMusicFolder");
}

void AndroidPlatform::probeSelectedFolder() {
    const auto context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(kBridge, "probeMusicFolder",
            "(Landroid/content/Context;)V", context.object());
}

void AndroidPlatform::finishActivity() {
    QNativeInterface::QAndroidApplication::context().callMethod<void>("finishAfterShutdown");
}

} // namespace mixxx::android

extern "C" JNIEXPORT void JNICALL
Java_io_github_djoshi_bitedj_PlatformBridge_nativeRequestQuit(JNIEnv*, jclass) {
    if (auto* app = QCoreApplication::instance()) {
        QMetaObject::invokeMethod(app, &QCoreApplication::quit, Qt::QueuedConnection);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_djoshi_bitedj_PlatformBridge_nativeDocumentResult(JNIEnv*, jclass,
        jstring metadata, jint ownedFd, jlong startOffset, jlong length, jstring error) {
    using namespace mixxx::android;
    DocumentResult result;
    result.metadata = QJsonDocument::fromJson(QJniObject(metadata).toString().toUtf8()).object();
    result.error = QJniObject(error).toString();
    if (ownedFd >= 0) {
        result.input = std::make_shared<DocumentInput>(ownedFd, startOffset, length);
    }
    // Protect the receiver until Qt has registered the queued call. Qt discards
    // it if the receiver dies; the captured input then closes its owned fd.
    std::lock_guard lock(s_receiverMutex);
    if (s_receiver) {
        auto* receiver = s_receiver;
        QMetaObject::invokeMethod(receiver,
                [receiver, result = std::move(result)]() {
                    emit receiver->documentOpened(result);
                }, Qt::QueuedConnection);
    }
}
