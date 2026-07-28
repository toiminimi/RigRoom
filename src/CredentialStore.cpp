#include "CredentialStore.h"

#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#ifdef signals
#undef signals
#endif
#include <libsecret/secret.h>
#include <cstring>

namespace {
const SecretSchema kRigRoomSchema = {
    "org.rigroom.RigRoom", SECRET_SCHEMA_NONE,
    {{"key", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, static_cast<SecretSchemaAttributeType>(0)}}
};

QMutex keyMutex;
QString sessionKey;
bool migrated = false;

QString decodeLegacy(const QString& input, const char* key) {
    QByteArray data = QByteArray::fromBase64(input.toLatin1());
    const int keyLen = static_cast<int>(strlen(key));
    for (int i = 0; i < data.size(); ++i) data[i] = data[i] ^ key[i % keyLen];
    return QString::fromUtf8(data);
}

void migrateLegacyLocked() {
    if (migrated) return;
    migrated = true;
    QSettings current("RigRoom", "RigRoom");
    QString encrypted = current.value("tone3000_api_key").toString();
    if (encrypted.isEmpty()) {
        QSettings legacy("PedalBoard", "PedalBoard");
        encrypted = legacy.value("tone3000_api_key").toString();
        // LEGACY: fixed migration key for old PedalBoard XOR-encrypted format.
        // This is a one-time migration path only, not a security mechanism.
        if (!encrypted.isEmpty()) encrypted = decodeLegacy(encrypted, "PedalBoardSecureKey123");
    } else {
        encrypted = decodeLegacy(encrypted, "RigRoomSecureKey123");
    }
    if (!encrypted.isEmpty()) {
        GError* error = nullptr;
        if (secret_password_store_sync(&kRigRoomSchema, SECRET_COLLECTION_DEFAULT,
                                       "RigRoom TONE3000 API key", encrypted.toUtf8().constData(),
                                       nullptr, &error, "key", "tone3000_api_key", nullptr)) {
            current.remove("tone3000_api_key");
            QSettings("PedalBoard", "PedalBoard").remove("tone3000_api_key");
        }
        if (error) g_error_free(error);
    }
}
}

QString CredentialStore::tone3000ApiKey() {
    QMutexLocker lock(&keyMutex);
    migrateLegacyLocked();
    if (!sessionKey.isEmpty()) return sessionKey;
    GError* error = nullptr;
    gchar* value = secret_password_lookup_sync(&kRigRoomSchema, nullptr, &error, "key", "tone3000_api_key", nullptr);
    if (error) g_error_free(error);
    if (!value) return {};
    sessionKey = QString::fromUtf8(value);
    secret_password_free(value);
    return sessionKey;
}

bool CredentialStore::setTone3000ApiKey(const QString& key, QString* errorText) {
    QMutexLocker lock(&keyMutex);
    migrated = true;
    sessionKey = key;
    GError* error = nullptr;
    const bool stored = secret_password_store_sync(&kRigRoomSchema, SECRET_COLLECTION_DEFAULT,
                                                   "RigRoom TONE3000 API key", key.toUtf8().constData(),
                                                   nullptr, &error, "key", "tone3000_api_key", nullptr);
    if (!stored && errorText) *errorText = error ? QString::fromUtf8(error->message) : "Secure storage is unavailable.";
    if (error) g_error_free(error);
    return stored;
}

bool CredentialStore::clearTone3000ApiKey(QString* errorText) {
    QMutexLocker lock(&keyMutex);
    sessionKey.clear();
    GError* error = nullptr;
    const bool cleared = secret_password_clear_sync(&kRigRoomSchema, nullptr, &error, "key", "tone3000_api_key", nullptr);
    if (!cleared && errorText) *errorText = error ? QString::fromUtf8(error->message) : "Secure storage is unavailable.";
    if (error) g_error_free(error);
    return cleared;
}
