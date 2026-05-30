#include "datamanager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QDateTime>
#include <QCryptographicHash>
#include <QStandardPaths>

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

    QJsonDocument doc(buildAuthJson(account));
    QFile file(authFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = "无法写入文件: " + authFile;
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

bool DataManager::writeTempAuthFile(const QJsonObject& account, QString& tmpPath, QString& errorMsg) const
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    tmpPath = tempDir + "/codex_auth_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";

    QJsonDocument doc(buildAuthJson(account));
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMsg = "无法创建临时文件: " + tmpPath;
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return true;
}
