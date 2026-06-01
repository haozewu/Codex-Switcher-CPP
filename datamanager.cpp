#include "datamanager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDateTime>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QUuid>
#include <QSaveFile>

DataManager::DataManager(QObject* parent)
    : QObject(parent)
{
    m_baseDir = QCoreApplication::applicationDirPath();
    m_dataDir = m_baseDir + "/data";
}

QString DataManager::baseDir() const
{
    return m_baseDir;
}

QString DataManager::dataDir() const
{
    return m_dataDir;
}

QString DataManager::accountsFilePath() const
{
    return m_dataDir + "/accounts.json";
}

QString DataManager::cacheFilePath() const
{
    return m_dataDir + "/usage_cache.json";
}

QString DataManager::settingsFilePath() const
{
    return m_dataDir + "/settings.json";
}

QString DataManager::codexAuthDir() const
{
    return QDir::homePath() + "/.codex";
}

QString DataManager::codexAuthFilePath() const
{
    return codexAuthDir() + "/auth.json";
}

QString DataManager::codexInstallationIdFilePath() const
{
    return codexAuthDir() + "/installation_id";
}

void DataManager::migrateDataFiles()
{
    QDir().mkpath(m_dataDir);
    QStringList files = {"accounts.json", "usage_cache.json", "settings.json"};
    for (const QString& fname : files) {
        QString oldPath = m_baseDir + "/" + fname;
        QString newPath = m_dataDir + "/" + fname;
        if (QFile::exists(oldPath) && !QFile::exists(newPath)) {
            QFile::copy(oldPath, newPath);
        }
    }
}

QJsonArray DataManager::loadAccounts()
{
    QString path = accountsFilePath();
    QString oldPath = m_baseDir + "/accounts.json";
    QString targetPath = QFile::exists(path) ? path : (QFile::exists(oldPath) ? oldPath : path);

    QFile file(targetPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonArray();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return QJsonArray();

    return doc.array();
}

bool DataManager::isValidUuid(const QString& value)
{
    const QUuid uuid(value.trimmed());
    return !uuid.isNull();
}

static QString normalizedUuid(const QString& value)
{
    return QUuid(value.trimmed()).toString(QUuid::WithoutBraces).toLower();
}

static bool shouldEncodeAccountPathChar(const QChar& ch)
{
    return ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' ||
           ch == '|' || ch == '?' || ch == '*' || ch == '%' || ch == '.' || ch == ' ' ||
           ch.unicode() < 32;
}

QString DataManager::newInstallationId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

bool DataManager::atomicWriteTextFile(const QString& path, const QString& content, QString& errorMsg)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        errorMsg = "创建目录失败: " + info.absolutePath();
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = "无法写入文件: " + path;
        return false;
    }
    file.write(content.toUtf8());
    if (!file.commit()) {
        errorMsg = "原子写入失败: " + path;
        return false;
    }
    return true;
}

QString DataManager::accountDataDirPath(const QJsonObject& account) const
{
    QString name = account.value("name").toString("account").trimmed();
    if (name.isEmpty())
        name = "account";

    QString safeName;
    for (const QChar& ch : name) {
        if (shouldEncodeAccountPathChar(ch)) {
            safeName += "%" + QString("%1").arg(ch.unicode(), 4, 16, QChar('0')).toUpper();
        } else {
            safeName += ch;
        }
    }
    if (safeName.isEmpty())
        safeName = "account";

    return m_dataDir + "/accounts/" + safeName;
}

QString DataManager::accountInstallationIdFilePath(const QJsonObject& account) const
{
    return accountDataDirPath(account) + "/installation_id";
}

QString DataManager::accountAuthFilePath(const QJsonObject& account) const
{
    return accountDataDirPath(account) + "/auth.json";
}

bool DataManager::authJsonBytesForAccount(const QJsonObject& account, QByteArray& bytes, QString& errorMsg) const
{
    const QString storedAuthPath = accountAuthFilePath(account);
    if (QFile::exists(storedAuthPath)) {
        QFile stored(storedAuthPath);
        if (!stored.open(QIODevice::ReadOnly)) {
            errorMsg = "无法读取账号原始 auth.json: " + storedAuthPath;
            return false;
        }
        bytes = stored.readAll();
        stored.close();
        if (bytes.trimmed().isEmpty()) {
            errorMsg = "账号原始 auth.json 为空: " + storedAuthPath;
            return false;
        }
        return true;
    }

    QJsonDocument doc(buildAuthJson(account));
    bytes = doc.toJson(QJsonDocument::Indented);
    return true;
}

bool DataManager::removeAccountData(const QJsonObject& account, QString& errorMsg) const
{
    const QString dirPath = accountDataDirPath(account);
    QDir dir(dirPath);
    if (!dir.exists())
        return true;

    if (!dir.removeRecursively()) {
        errorMsg = "删除账号数据目录失败: " + dirPath;
        return false;
    }
    return true;
}

bool DataManager::importCurrentAuthAccount(QJsonObject& account, QString& errorMsg) const
{
    QFile file(codexAuthFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMsg = "无法读取当前 Codex auth.json: " + codexAuthFilePath();
        return false;
    }

    QJsonParseError parseError;
    const QByteArray raw = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        errorMsg = "当前 Codex auth.json 不是有效 JSON: " + parseError.errorString();
        return false;
    }

    const QJsonObject auth = doc.object();
    const QJsonObject tokens = auth.value("tokens").toObject();
    if (tokens.isEmpty()) {
        errorMsg = "当前 Codex auth.json 未包含 tokens 对象。";
        return false;
    }

    QJsonObject credentials;
    const QStringList tokenFields = {
        "id_token", "account_id", "mail_token", "access_token",
        "refresh_token", "last_refresh", "chatgpt_account_id"
    };
    for (const QString& field : tokenFields) {
        if (tokens.contains(field))
            credentials[field] = tokens.value(field);
    }

    if (credentials.value("access_token").toString().isEmpty() &&
        credentials.value("refresh_token").toString().isEmpty()) {
        errorMsg = "当前 Codex auth.json 未包含可导入的 access_token 或 refresh_token。";
        return false;
    }

    QString label = tokens.value("email").toString().trimmed();
    if (label.isEmpty())
        label = tokens.value("account_id").toString().trimmed();
    if (label.isEmpty())
        label = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

    account = QJsonObject();
    account["name"] = QString::fromUtf8("当前配置 %1").arg(label);
    account["credentials"] = credentials;
    account["imported_from"] = "current_auth";
    account["imported_at"] = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddThh:mm:ss.zzzZ");

    if (!ensureAccountInstallationId(account, errorMsg))
        return false;

    const QString authCopyPath = accountAuthFilePath(account);
    const QFileInfo info(authCopyPath);
    if (!QDir().mkpath(info.absolutePath())) {
        errorMsg = "创建账号数据目录失败: " + info.absolutePath();
        return false;
    }

    QFile copy(authCopyPath);
    if (!copy.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = "无法复制当前 auth.json 到: " + authCopyPath;
        return false;
    }
    copy.write(raw);
    copy.close();

    return true;
}

bool DataManager::ensureAccountInstallationId(QJsonObject& account, QString& errorMsg) const
{
    const QString path = accountInstallationIdFilePath(account);
    QString id = account.value("installation_id").toString().trimmed();

    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString fileId = QString::fromUtf8(existing.readAll()).trimmed();
        existing.close();
        if (isValidUuid(fileId)) {
            const QString normalized = normalizedUuid(fileId);
            if (normalized != fileId && !atomicWriteTextFile(path, normalized, errorMsg))
                return false;
            account["installation_id"] = normalized;
            return true;
        }
    }

    if (!isValidUuid(id))
        id = newInstallationId();
    else
        id = normalizedUuid(id);

    if (!atomicWriteTextFile(path, id, errorMsg))
        return false;

    account["installation_id"] = id;
    return true;
}

bool DataManager::ensureAccountInstallationIds(QJsonArray& accounts, QString& errorMsg) const
{
    for (int i = 0; i < accounts.size(); ++i) {
        QJsonObject account = accounts.at(i).toObject();
        if (!ensureAccountInstallationId(account, errorMsg))
            return false;
        accounts.replace(i, account);
    }
    return true;
}

bool DataManager::saveAccounts(const QJsonArray& accounts)
{
    QDir().mkpath(m_dataDir);
    QFile file(accountsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonDocument doc(accounts);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonObject DataManager::loadCache()
{
    QFile file(cacheFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonObject();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();

    return doc.object();
}

bool DataManager::saveCache(const QJsonObject& cache)
{
    QDir().mkpath(m_dataDir);
    QFile file(cacheFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonDocument doc(cache);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonObject DataManager::loadSettings()
{
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonObject();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();

    return doc.object();
}

bool DataManager::saveSettings(const QJsonObject& settings)
{
    QDir().mkpath(m_dataDir);
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonDocument doc(settings);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QString DataManager::accountKey(const QJsonObject& account)
{
    QJsonObject creds = account.value("credentials").toObject();
    QString cgId = creds.value("chatgpt_account_id").toString();
    if (cgId.isEmpty())
        cgId = creds.value("account_id").toString();
    if (!cgId.isEmpty())
        return "id:" + cgId;

    QString token = creds.value("access_token").toString();
    if (!token.isEmpty()) {
        QByteArray hash = QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256);
        return "tok:" + hash.toHex().left(16);
    }

    return "name:" + account.value("name").toString();
}

QJsonObject DataManager::buildAuthJson(const QJsonObject& account)
{
    QJsonObject credentials = account.value("credentials").toObject();
    QString nowStr = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddThh:mm:ss.zzzZ");

    QJsonObject auth;
    auth["OPENAI_API_KEY"] = QJsonValue::Null;
    auth["last_refresh"] = nowStr;

    QJsonObject tokens;
    tokens["id_token"] = credentials.value("id_token");
    tokens["account_id"] = credentials.value("account_id");
    tokens["mail_token"] = credentials.value("mail_token").toString("tok_dummy");
    tokens["access_token"] = credentials.value("access_token");
    tokens["last_refresh"] = credentials.value("last_refresh").toString(nowStr);
    tokens["refresh_token"] = credentials.value("refresh_token");

    QString cgId = credentials.value("chatgpt_account_id").toString();
    if (cgId.isEmpty())
        cgId = credentials.value("account_id").toString();
    tokens["chatgpt_account_id"] = cgId;

    auth["tokens"] = tokens;
    return auth;
}

QJsonObject DataManager::parseUsageResponse(const QJsonObject& data, const QString& accName)
{
    QJsonObject info;
    info["account"] = accName;
    info["plan"] = data.value("plan_type");
    if (info["plan"].isNull()) info["plan"] = data.value("plan");
    if (info["plan"].isNull()) info["plan"] = data.value("subscription_plan");

    QJsonObject rl = data.value("rate_limit").toObject();
    info["rate_limit_allowed"] = rl.value("allowed");
    info["rate_limit_reached"] = rl.value("limit_reached");

    QJsonObject primary, secondary;
    QJsonObject pw = rl.value("primary_window").toObject();
    if (!pw.isEmpty()) {
        if (!pw.value("used_percent").isNull()) {
            double used = pw.value("used_percent").toDouble();
            primary["used_pct"] = used;
            primary["remaining_pct"] = 100.0 - used;
        }
        primary["window_seconds"] = pw.value("limit_window_seconds");
        primary["reset_after_seconds"] = pw.value("reset_after_seconds");
        primary["reset_at_dt"] = pw.value("reset_at").toDouble();
    }

    QJsonObject sw = rl.value("secondary_window").toObject();
    if (!sw.isEmpty()) {
        if (!sw.value("used_percent").isNull()) {
            double used = sw.value("used_percent").toDouble();
            secondary["used_pct"] = used;
            secondary["remaining_pct"] = 100.0 - used;
        }
        secondary["window_seconds"] = sw.value("limit_window_seconds");
        secondary["reset_after_seconds"] = sw.value("reset_after_seconds");
        secondary["reset_at_dt"] = sw.value("reset_at").toDouble();
    }

    info["primary"] = primary;
    info["secondary"] = secondary;
    return info;
}

bool DataManager::backupAndWriteAuthFile(const QJsonObject& account, QString& errorMsg) const
{
    QString authDir = codexAuthDir();
    QString authFile = codexAuthFilePath();
    QString installationIdFile = codexInstallationIdFilePath();
    QJsonObject accountWithId = account;
    if (!ensureAccountInstallationId(accountWithId, errorMsg))
        return false;

    QDir dir;
    if (!dir.mkpath(authDir)) {
        errorMsg = "创建目录 " + authDir + " 失败";
        return false;
    }

    if (QFile::exists(authFile)) {
        QString backupName = "auth.json.bak_" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
        QString backupPath = authDir + "/" + backupName;
        if (!QFile::copy(authFile, backupPath)) {
            errorMsg = "创建备份失败: " + backupPath;
            return false;
        }
    }
    if (QFile::exists(installationIdFile)) {
        QString backupName = "installation_id.bak_" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
        QString backupPath = authDir + "/" + backupName;
        if (!QFile::copy(installationIdFile, backupPath)) {
            errorMsg = "创建 installation_id 备份失败: " + backupPath;
            return false;
        }
    }

    QByteArray authBytes;
    if (!authJsonBytesForAccount(accountWithId, authBytes, errorMsg))
        return false;

    QFile file(authFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = "无法写入文件: " + authFile;
        return false;
    }
    file.write(authBytes);
    file.close();

    if (!atomicWriteTextFile(installationIdFile, accountWithId.value("installation_id").toString(), errorMsg))
        return false;

    return true;
}

bool DataManager::writeTempAuthFile(const QJsonObject& account, QString& tmpPath, QString& errorMsg) const
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    tmpPath = tempDir + "/codex_auth_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";

    QByteArray authBytes;
    if (!authJsonBytesForAccount(account, authBytes, errorMsg))
        return false;

    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = "无法创建临时文件: " + tmpPath;
        return false;
    }
    file.write(authBytes);
    file.close();

    return true;
}

bool DataManager::writeTempInstallationIdFile(const QJsonObject& account, QString& tmpPath, QString& errorMsg) const
{
    QJsonObject accountWithId = account;
    if (!ensureAccountInstallationId(accountWithId, errorMsg))
        return false;

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    tmpPath = tempDir + "/codex_installation_id_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tmp";

    return atomicWriteTextFile(tmpPath, accountWithId.value("installation_id").toString(), errorMsg);
}

bool DataManager::writeTempCurrentAuthFile(QString& tmpPath, QString& errorMsg) const
{
    QFile source(codexAuthFilePath());
    if (!source.open(QIODevice::ReadOnly)) {
        errorMsg = "无法读取本机当前 auth.json: " + codexAuthFilePath();
        return false;
    }
    const QByteArray bytes = source.readAll();
    source.close();

    if (bytes.trimmed().isEmpty()) {
        errorMsg = "本机当前 auth.json 为空: " + codexAuthFilePath();
        return false;
    }

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    tmpPath = tempDir + "/codex_current_auth_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";

    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMsg = "无法创建临时文件: " + tmpPath;
        return false;
    }
    file.write(bytes);
    file.close();

    return true;
}

bool DataManager::writeTempCurrentInstallationIdFile(QString& tmpPath, QString& errorMsg) const
{
    QFile source(codexInstallationIdFilePath());
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMsg = "无法读取本机当前 installation_id: " + codexInstallationIdFilePath();
        return false;
    }
    const QString id = QString::fromUtf8(source.readAll()).trimmed();
    source.close();

    if (id.isEmpty()) {
        errorMsg = "本机当前 installation_id 为空: " + codexInstallationIdFilePath();
        return false;
    }

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    tmpPath = tempDir + "/codex_current_installation_id_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tmp";

    return atomicWriteTextFile(tmpPath, id, errorMsg);
}
