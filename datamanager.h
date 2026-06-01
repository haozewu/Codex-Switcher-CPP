#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class DataManager : public QObject
{
    Q_OBJECT

public:
    explicit DataManager(QObject* parent = nullptr);

    QString baseDir() const;
    QString dataDir() const;
    QString accountsFilePath() const;
    QString cacheFilePath() const;
    QString settingsFilePath() const;
    QString codexAuthDir() const;
    QString codexAuthFilePath() const;
    QString codexInstallationIdFilePath() const;

    void migrateDataFiles();

    QJsonArray loadAccounts();
    bool saveAccounts(const QJsonArray& accounts);
    bool importCurrentAuthAccount(QJsonObject& account, QString& errorMsg) const;
    bool ensureAccountInstallationIds(QJsonArray& accounts, QString& errorMsg) const;
    bool ensureAccountInstallationId(QJsonObject& account, QString& errorMsg) const;
    bool removeAccountData(const QJsonObject& account, QString& errorMsg) const;

    QJsonObject loadCache();
    bool saveCache(const QJsonObject& cache);

    QJsonObject loadSettings();
    bool saveSettings(const QJsonObject& settings);

    static QString accountKey(const QJsonObject& account);
    static QJsonObject buildAuthJson(const QJsonObject& account);
    static QJsonObject parseUsageResponse(const QJsonObject& data, const QString& accName);

    bool backupAndWriteAuthFile(const QJsonObject& account, QString& errorMsg) const;
    bool writeTempAuthFile(const QJsonObject& account, QString& tmpPath, QString& errorMsg) const;
    bool writeTempInstallationIdFile(const QJsonObject& account, QString& tmpPath, QString& errorMsg) const;
    bool writeTempCurrentAuthFile(QString& tmpPath, QString& errorMsg) const;
    bool writeTempCurrentInstallationIdFile(QString& tmpPath, QString& errorMsg) const;

private:
    QString accountDataDirPath(const QJsonObject& account) const;
    QString accountAuthFilePath(const QJsonObject& account) const;
    QString accountInstallationIdFilePath(const QJsonObject& account) const;
    bool authJsonBytesForAccount(const QJsonObject& account, QByteArray& bytes, QString& errorMsg) const;
    static bool isValidUuid(const QString& value);
    static QString newInstallationId();
    static bool atomicWriteTextFile(const QString& path, const QString& content, QString& errorMsg);

    QString m_baseDir;
    QString m_dataDir;
};

#endif // DATAMANAGER_H
