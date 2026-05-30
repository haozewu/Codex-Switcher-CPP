#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QObject>
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

    void migrateDataFiles();

    QJsonArray loadAccounts();
    bool saveAccounts(const QJsonArray& accounts);

    QJsonObject loadCache();
    bool saveCache(const QJsonObject& cache);

    QJsonObject loadSettings();
    bool saveSettings(const QJsonObject& settings);

    static QString accountKey(const QJsonObject& account);
    static QJsonObject buildAuthJson(const QJsonObject& account);
    static QJsonObject parseUsageResponse(const QJsonObject& data, const QString& accName);

    bool backupAndWriteAuthFile(const QJsonObject& account, QString& errorMsg) const;
    bool writeTempAuthFile(const QJsonObject& account, QString& tmpPath, QString& errorMsg) const;

private:
    QString m_baseDir;
    QString m_dataDir;
};

#endif // DATAMANAGER_H
