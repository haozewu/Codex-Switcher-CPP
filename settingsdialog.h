#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QLineEdit;
class QProcess;
class QPushButton;
class QSpinBox;
class QLabel;
class QTimer;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(int intervalMinutes, int activeIntervalMinutes,
                            int quotaAlertThreshold,
                            const QJsonObject& remoteConfig, QWidget* parent = nullptr);
    int intervalMinutes() const;
    int activeIntervalMinutes() const;
    int quotaAlertThreshold() const;
    QJsonObject remoteConfig() const;
    int resultMinutes() const;
    QJsonObject resultRemote() const;

private:
    void testConnection();
    void updateRemoteEnabled(bool enabled);
    void accept() override;

    QSpinBox* m_interval = nullptr;
    QSpinBox* m_activeInterval = nullptr;
    QSpinBox* m_quotaAlertThreshold = nullptr;
    QCheckBox* m_enableRemote = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_host = nullptr;
    QSpinBox* m_port = nullptr;
    QPushButton* m_testButton = nullptr;
    QLabel* m_testLabel = nullptr;
    QProcess* m_testProcess = nullptr;
    QTimer* m_testTimeout = nullptr;
    bool m_testPassed = false;
    bool m_testTimedOut = false;
};

#endif // SETTINGSDIALOG_H
