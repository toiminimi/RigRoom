#pragma once

#include <QString>

namespace CredentialStore {
QString tone3000ApiKey();
bool setTone3000ApiKey(const QString& key, QString* error = nullptr);
bool clearTone3000ApiKey(QString* error = nullptr);
}
