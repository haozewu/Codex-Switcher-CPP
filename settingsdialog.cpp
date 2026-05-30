#include "settingsdialog.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(int intervalMinutes, const QJsonObject& remoteConfig, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("设置");
    setMinimumWidth(460);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(14);

    auto* title = new QLabel("应用设置");
    title->setObjectName("dialogTitle");
    root->addWidget(title);

    auto* form = new QFormLayout();
    m_interval = new QSpinBox();
    m_interval->setRange(1, 1440);
    m_interval->setValue(qMax(1, intervalMinutes));
    m_interval->setSuffix(" 分钟");
    form->addRow("自动查询间隔", m_interval);

    m_enableRemote = new QCheckBox("启用云端替换");
    m_enableRemote->setChecked(remoteConfig.value("enabled").toBool(false));
    form->addRow("", m_enableRemote);

    m_user = new QLineEdit(remoteConfig.value("user").toString("haoze"));
    m_host = new QLineEdit(remoteConfig.value("host").toString("127.0.0.1"));
    m_port = new QSpinBox();
    m_port->setRange(1, 65535);
    m_port->setValue(remoteConfig.value("port").toInt(9002));
    form->addRow("SSH 用户", m_user);
    form->addRow("SSH 地址", m_host);
    form->addRow("SSH 端口", m_port);
    root->addLayout(form);

    auto* testRow = new QHBoxLayout();
    m_testButton = new QPushButton("测试连接");
    m_testButton->setObjectName("softButton");
    m_testLabel = new QLabel("未测试");
    m_testLabel->setObjectName("mutedText");
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_testLabel, 1);
    root->addLayout(testRow);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* cancel = new QPushButton("取消");
    cancel->setObjectName("ghostButton");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* ok = new QPushButton("保存");
    ok->setObjectName("primaryButton");
    connect(ok, &QPushButton::clicked, this, &SettingsDialog::accept);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    root->addLayout(buttons);

    m_testTimeout = new QTimer(this);
    m_testTimeout->setSingleShot(true);
    connect(m_testTimeout, &QTimer::timeout, this, [this]() {
        if (!m_testProcess)
            return;
        m_testTimedOut = true;
        m_testProcess->kill();
        m_testPassed = false;
        m_testLabel->setText("连接超时，请检查地址/端口/密钥");
        m_testButton->setEnabled(true);
    });

    connect(m_testButton, &QPushButton::clicked, this, &SettingsDialog::testConnection);
    connect(m_enableRemote, &QCheckBox::toggled, this, &SettingsDialog::updateRemoteEnabled);
    auto resetTest = [this]() {
        m_testPassed = false;
        m_testLabel->setText("配置已变更，需重新测试");
    };
    connect(m_user, &QLineEdit::textChanged, this, resetTest);
    connect(m_host, &QLineEdit::textChanged, this, resetTest);
    connect(m_port, qOverload<int>(&QSpinBox::valueChanged), this, resetTest);
    updateRemoteEnabled(m_enableRemote->isChecked());
}

int SettingsDialog::intervalMinutes() const
{
    return m_interval->value();
}

QJsonObject SettingsDialog::remoteConfig() const
{
    QJsonObject obj;
    obj["enabled"] = m_enableRemote->isChecked();
    obj["user"] = m_user->text().trimmed();
    obj["host"] = m_host->text().trimmed();
    obj["port"] = m_port->value();
    return obj;
}

int SettingsDialog::resultMinutes() const
{
    return intervalMinutes();
}

QJsonObject SettingsDialog::resultRemote() const
{
    return remoteConfig();
}

void SettingsDialog::testConnection()
{
    if (m_testProcess && m_testProcess->state() != QProcess::NotRunning)
        return;

    m_testPassed = false;
    m_testTimedOut = false;
    m_testButton->setEnabled(false);
    m_testLabel->setText("测试中...");

    m_testProcess = new QProcess(this);
    QStringList args;
    args << "-o" << "BatchMode=yes"
         << "-o" << "ConnectTimeout=5"
         << "-o" << "StrictHostKeyChecking=no"
         << "-o" << "UserKnownHostsFile=NUL"
         << "-o" << "NumberOfPasswordPrompts=0"
         << "-T"
         << "-p" << QString::number(m_port->value())
         << QString("%1@%2").arg(m_user->text().trimmed(), m_host->text().trimmed())
         << "echo ok";

    connect(m_testProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        if (m_testTimeout)
            m_testTimeout->stop();
        const QString out = QString::fromUtf8(m_testProcess->readAllStandardOutput());
        const QString err = QString::fromUtf8(m_testProcess->readAllStandardError());
        if (m_testTimedOut) {
            m_testPassed = false;
            m_testLabel->setText("连接超时，请检查地址/端口/密钥");
            m_testButton->setEnabled(true);
            m_testProcess->deleteLater();
            m_testProcess = nullptr;
            return;
        }
        m_testPassed = (status == QProcess::NormalExit && code == 0 && out.contains("ok"));
        const QString message = err.trimmed().isEmpty() ? QString("退出码 %1").arg(code) : err.trimmed().left(160);
        m_testLabel->setText(m_testPassed ? "连接成功" : ("连接失败: " + message));
        m_testButton->setEnabled(true);
        m_testProcess->deleteLater();
        m_testProcess = nullptr;
    });
    connect(m_testProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_testTimeout)
            m_testTimeout->stop();
        m_testPassed = false;
        m_testLabel->setText("无法启动 ssh，请确认已安装 OpenSSH");
        m_testButton->setEnabled(true);
        if (m_testProcess) {
            m_testProcess->deleteLater();
            m_testProcess = nullptr;
        }
    });
    connect(m_testProcess, &QProcess::started, this, [this]() {
        m_testLabel->setText("已启动 ssh，等待返回...");
    });

    m_testProcess->start("ssh", args);
    m_testTimeout->start(8000);
}

void SettingsDialog::updateRemoteEnabled(bool enabled)
{
    m_user->setEnabled(true);
    m_host->setEnabled(true);
    m_port->setEnabled(true);
    m_testButton->setEnabled(true);
    if (!enabled) {
        m_testPassed = false;
        m_testLabel->setText("可先测试，启用后保存需要测试成功");
    }
}

void SettingsDialog::accept()
{
    if (m_enableRemote->isChecked() && !m_testPassed) {
        QMessageBox::warning(this, "需要测试连接", "启用云端替换前，请先测试连接成功。");
        return;
    }
    QDialog::accept();
}
