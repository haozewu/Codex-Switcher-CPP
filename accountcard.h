#ifndef ACCOUNTCARD_H
#define ACCOUNTCARD_H

#include <QFrame>
#include <QJsonObject>

class QLabel;
class QPushButton;
class QProgressBar;

class AccountCard : public QFrame
{
    Q_OBJECT

public:
    explicit AccountCard(const QJsonObject& account, bool remoteEnabled, bool active, QWidget* parent = nullptr);

    QString accountName() const;
    QJsonObject account() const;
    void setActive(bool active);
    void setLoading(bool loading);
    void setResult(const QJsonObject& result);
    void refreshRelativeTimes();

signals:
    void queryRequested(const QString& accountName);
    void switchRequested(const QString& accountName);
    void deleteRequested(const QString& accountName);

private:
    void setLimit(QProgressBar* bar, QLabel* percent, QLabel* meta, const QJsonObject& limit);
    void refreshLimitTime(QLabel* meta, const QJsonObject& limit);
    void applyCurrentStatus();
    static QString levelForRemaining(double remaining);
    static QString formatSeconds(int seconds);
    static QString formatReset(double epochSeconds);
    void refreshStatusStyle(const QString& state);

    QJsonObject m_account;
    QJsonObject m_lastResult;
    QString m_accountName;
    bool m_isActive = false;
    QLabel* m_statusBadge = nullptr;
    QLabel* m_planBadge = nullptr;
    QLabel* m_message = nullptr;
    QLabel* m_primaryPercent = nullptr;
    QLabel* m_primaryMeta = nullptr;
    QLabel* m_secondaryPercent = nullptr;
    QLabel* m_secondaryMeta = nullptr;
    QProgressBar* m_primaryBar = nullptr;
    QProgressBar* m_secondaryBar = nullptr;
    QPushButton* m_queryButton = nullptr;
};

#endif // ACCOUNTCARD_H
