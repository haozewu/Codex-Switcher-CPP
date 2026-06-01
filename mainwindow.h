#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QProcess>

class AccountCard;
class DataManager;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

public slots:
    void showWindow();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void importAccount();
    void importCurrentAccount();
    void checkAllQuotas();
    void openSettings();
    void reloadAccounts();

    void checkQuota(const QString& accountName);
    void switchAccount(const QString& accountName);
    void deleteAccount(const QString& accountName);

    void autoQueryCheck();
    void trayActivated(QSystemTrayIcon::ActivationReason reason);
    void trayExit();

private:
    void setupUI();
    void setupTray();
    void loadState();
    void rebuildCards();
    void rebuildCardsPreservingScroll();
    void relayoutCards();
    void refreshCardTimes();
    QJsonObject restoreResult(const QJsonObject& account) const;

    QJsonObject accountByName(const QString& name) const;
    QJsonObject accountByKey(const QString& key) const;
    int accountIndex(const QString& name) const;

    void sortAccounts();
    void keepActiveAccountFirst();
    void saveState();

    void startUsageQuery(const QString& accountName, const QString& accessToken, const QString& key);
    void onUsageReplyFinished(QNetworkReply* reply, const QString& name, const QString& token,
                              const QString& key, int stage);

    void cacheResult(const QJsonObject& account, const QJsonObject& result);
    void maybeShowQuotaAlert(const QJsonObject& account, const QJsonObject& result);
    double cacheAgeSeconds(const QJsonObject& account) const;
    void normalizeAutoQuerySchedule();
    qint64 nextAutoQueryAtMs(const QJsonObject& account) const;
    void scheduleNextAutoQuery(int minimumDelayMs = 0);

    void onRemoteAuthCheckFinished(int exitCode);
    void onRemoteBackupFinished(int exitCode);
    void onScpAuthUploadFinished(int exitCode);
    void onScpInstallationIdUploadFinished(int exitCode);
    void cleanupCloudTemp();
    void startCloudSwitch(const QString& accountName);
    void restartLocalCodex();
    void restartRemoteCodex();
    void onRemoteRestartFinished(int exitCode);
    void finishCloudSwitch(bool ok, const QString& message);
    void finishLocalRestart(bool ok, const QString& message);
    void maybeShowSwitchSummary();

    AccountCard* findCard(const QString& name) const;

    DataManager* m_dm;
    QNetworkAccessManager* m_nam;
    QTimer* m_autoQueryTimer;
    QSystemTrayIcon* m_trayIcon;
    bool m_trayAvailable;
    bool m_quitFromTray;

    QJsonArray m_accounts;
    QJsonObject m_cache;
    int m_queryIntervalMinutes;
    int m_activeQueryIntervalMinutes;
    int m_quotaAlertThreshold;
    QString m_activeAccountKey;
    QJsonObject m_remoteConfig;
    QSet<QString> m_queryingKeys;
    QSet<QString> m_quotaAlertedKeys;

    QScrollArea* m_scrollArea;
    QWidget* m_cardContainer;
    QGridLayout* m_cardGrid;
    QList<AccountCard*> m_cards;
    int m_cardColumns;

    QLabel* m_countBadge;
    QLabel* m_fileLabel;

    QProcess* m_cloudProcess;
    QString m_cloudAccountName;
    QString m_cloudTmpPath;
    QString m_cloudInstallationTmpPath;
    enum CloudStage { CheckAuth, Backup, UploadAuth, UploadInstallationId, Restart };
    CloudStage m_cloudStage;
    bool m_switchInProgress = false;
    bool m_waitingLocalRestart = false;
    bool m_waitingCloudSwitch = false;
    bool m_localSwitchOk = false;
    bool m_cloudSwitchOk = false;
    QString m_switchAccountName;
    QString m_localSwitchMessage;
    QString m_cloudSwitchMessage;
};

#endif // MAINWINDOW_H
