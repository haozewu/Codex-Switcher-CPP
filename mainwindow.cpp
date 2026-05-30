#include "mainwindow.h"
#include "accountcard.h"
#include "datamanager.h"
#include "importdialog.h"
#include "settingsdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QApplication>
#include <algorithm>
#include <climits>
#include <QFileInfo>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStyle>
#include <QSizePolicy>
#include <QColor>
#include <QPixmap>

static QString findCodexExecutable()
{
    const QStringList executableNames = {"codex.exe", "codex.cmd", "codex"};
    for (const QString& name : executableNames) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString localAppData = env.value("LOCALAPPDATA");
    const QStringList aliasPaths = {
        localAppData + "/Microsoft/WindowsApps/codex.exe",
        localAppData + "/Microsoft/WindowsApps/codex.cmd",
        env.value("APPDATA") + "/npm/codex.cmd",
        env.value("USERPROFILE") + "/AppData/Roaming/npm/codex.cmd"
    };
    for (const QString& path : aliasPaths) {
        if (!path.startsWith("/") && QFileInfo::exists(path))
            return QDir::toNativeSeparators(path);
    }

    const QString programFiles = env.value("ProgramFiles", "C:/Program Files");
    QDir windowsApps(programFiles + "/WindowsApps");
    const QStringList packages = windowsApps.entryList({"OpenAI.Codex_*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (int i = packages.size() - 1; i >= 0; --i) {
        const QString path = windowsApps.absoluteFilePath(packages.at(i) + "/app/resources/codex.exe");
        if (QFileInfo::exists(path))
            return QDir::toNativeSeparators(path);
    }

    return QString();
}

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
    , m_dm(new DataManager(this))
    , m_nam(new QNetworkAccessManager(this))
    , m_trayIcon(nullptr)
    , m_trayAvailable(false)
    , m_quitFromTray(false)
    , m_queryIntervalMinutes(10)
    , m_cardColumns(2)
    , m_cloudProcess(nullptr)
    , m_cloudStage(CheckAuth)
{

    setObjectName("rootWidget");
    setWindowTitle(QString::fromUtf8("Codex 账号管理器"));
    setFixedSize(1380, 960);

    m_dm->migrateDataFiles();
    loadState();
    setupUI();
    setupTray();
    rebuildCards();

    m_autoQueryTimer = new QTimer(this);
    connect(m_autoQueryTimer, &QTimer::timeout, this, &MainWindow::autoQueryCheck);
    m_autoQueryTimer->start(30000);
}

MainWindow::~MainWindow()
{
    if (m_cloudProcess) {
        m_cloudProcess->kill();
    }
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
}

void MainWindow::setupUI()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(26, 20, 26, 22);
    root->setSpacing(12);

    QHBoxLayout* topBar = new QHBoxLayout();
    QVBoxLayout* titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);

    QLabel* appTitle = new QLabel(QString::fromUtf8("Codex 账号管理器"));
    appTitle->setObjectName("appTitle");

    QLabel* appSubtitle = new QLabel(QString::fromUtf8("多列卡片展示 | 自动排序 | 托盘驻留"));
    appSubtitle->setObjectName("mutedText");

    titleBox->addWidget(appTitle);
    titleBox->addWidget(appSubtitle);
    topBar->addLayout(titleBox, 1);

    m_countBadge = new QLabel();
    m_countBadge->setObjectName("countBadge");
    topBar->addWidget(m_countBadge);
    root->addLayout(topBar);

    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->setSpacing(10);

    QPushButton* importBtn = new QPushButton(QString::fromUtf8("+ 导入"));
    importBtn->setObjectName("primaryButton");
    importBtn->setCursor(Qt::PointingHandCursor);
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::importAccount);

    QPushButton* queryAllBtn = new QPushButton(QString::fromUtf8("\u21bb 全部查询"));
    queryAllBtn->setObjectName("softButton");
    queryAllBtn->setCursor(Qt::PointingHandCursor);
    connect(queryAllBtn, &QPushButton::clicked, this, &MainWindow::checkAllQuotas);

    QPushButton* settingsBtn = new QPushButton(QString::fromUtf8("\u2699 设置"));
    settingsBtn->setObjectName("ghostButton");
    settingsBtn->setCursor(Qt::PointingHandCursor);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);

    QPushButton* refreshBtn = new QPushButton(QString::fromUtf8("\u21c5 刷新"));
    refreshBtn->setObjectName("ghostButton");
    refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::reloadAccounts);

    toolbar->addWidget(importBtn);
    toolbar->addWidget(queryAllBtn);
    toolbar->addWidget(settingsBtn);
    toolbar->addWidget(refreshBtn);
    toolbar->addStretch();

    m_fileLabel = new QLabel(m_dm->accountsFilePath());
    m_fileLabel->setObjectName("pathText");
    toolbar->addWidget(m_fileLabel);
    root->addLayout(toolbar);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setObjectName("mainScroll");

    m_cardContainer = new QWidget();
    m_cardContainer->setObjectName("scrollContent");
    m_cardContainer->installEventFilter(this);
    m_cardGrid = new QGridLayout(m_cardContainer);
    m_cardGrid->setContentsMargins(0, 56, 4, 0);
    m_cardGrid->setHorizontalSpacing(22);
    m_cardGrid->setVerticalSpacing(18);
    m_cardGrid->setAlignment(Qt::AlignTop);

    m_scrollArea->setWidget(m_cardContainer);
    root->addWidget(m_scrollArea, 1);
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip(QString::fromUtf8("Codex 账号管理器"));

    QIcon icon(":/app-icon.ico");
    if (icon.isNull()) {
        QPixmap px(32, 32);
        px.fill(QColor("#4f46e5"));
        icon = QIcon(px);
    }
    m_trayIcon->setIcon(icon);

    QMenu* menu = new QMenu(this);
    QAction* showAction = menu->addAction(QString::fromUtf8("显示主窗口"));
    connect(showAction, &QAction::triggered, this, &MainWindow::showWindow);
    QAction* queryAllAction = menu->addAction(QString::fromUtf8("查询全部"));
    connect(queryAllAction, &QAction::triggered, this, &MainWindow::checkAllQuotas);
    menu->addSeparator();
    QAction* exitAction = menu->addAction(QString::fromUtf8("退出"));
    connect(exitAction, &QAction::triggered, this, &MainWindow::trayExit);

    m_trayIcon->setContextMenu(menu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);
    m_trayIcon->show();
    m_trayAvailable = true;
}

void MainWindow::loadState()
{
    m_accounts = m_dm->loadAccounts();
    m_cache = m_dm->loadCache();

    QJsonObject settings = m_dm->loadSettings();
    m_queryIntervalMinutes = settings.value("interval_minutes").toInt(10);
    if (m_queryIntervalMinutes < 1) m_queryIntervalMinutes = 10;

    m_remoteConfig = settings.value("remote_config").toObject();
    if (m_remoteConfig.isEmpty()) {
        m_remoteConfig["enabled"] = false;
        m_remoteConfig["user"] = "haoze";
        m_remoteConfig["host"] = "127.0.0.1";
        m_remoteConfig["port"] = 9002;
    }
}

void MainWindow::saveState()
{
    QJsonObject settings;
    settings["interval_minutes"] = m_queryIntervalMinutes;
    settings["remote_config"] = m_remoteConfig;
    m_dm->saveSettings(settings);
}

void MainWindow::rebuildCards()
{
    for (AccountCard* card : m_cards) {
        card->hide();
        card->setParent(nullptr);
        card->deleteLater();
    }
    m_cards.clear();

    while (m_cardGrid->count() > 0) {
        QLayoutItem* item = m_cardGrid->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (m_accounts.isEmpty()) {
        QLabel* empty = new QLabel(QString::fromUtf8("还没有导入账号。\n点击上方「+ 导入」粘贴 JSON 后即可开始查询。"));
        empty->setObjectName("emptyState");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        empty->setMinimumHeight(260);
        m_cardGrid->addWidget(empty, 0, 0, 1, 2);
        m_cardContainer->setMinimumHeight(280);
    } else {
        bool remoteEnabled = m_remoteConfig.value("enabled").toBool(false);
        for (int i = 0; i < m_accounts.size(); ++i) {
            QJsonObject acc = m_accounts[i].toObject();
            QString name = acc.value("name").toString();
            QString key = DataManager::accountKey(acc);

            AccountCard* card = new AccountCard(acc, remoteEnabled, m_cardContainer);
            connect(card, &AccountCard::queryRequested, this, &MainWindow::checkQuota);
            connect(card, &AccountCard::switchRequested, this, &MainWindow::switchAccount);
            connect(card, &AccountCard::cloudSwitchRequested, this, &MainWindow::cloudSwitchAccount);
            connect(card, &AccountCard::deleteRequested, this, &MainWindow::deleteAccount);

            if (m_queryingKeys.contains(key)) {
                card->setLoading(true);
            } else if (m_cache.contains(key)) {
                card->setResult(restoreResult(acc));
            }
            m_cards.append(card);
        }
    }

    relayoutCards();

    m_countBadge->setText(QString::fromUtf8("%1 个账号").arg(m_accounts.size()));
    m_fileLabel->setText(m_dm->accountsFilePath());
}

void MainWindow::relayoutCards()
{
    if (m_cards.isEmpty()) return;

    while (m_cardGrid->count() > 0) {
        QLayoutItem* item = m_cardGrid->takeAt(0);
        delete item;
    }
    for (int row = 0; row <= m_cards.size() + 1; ++row) {
        m_cardGrid->setRowStretch(row, 0);
    }

    int w = m_cardContainer->width();
    int cardMin = 640;
    int cols = qBound(1, w / cardMin, 2);
    m_cardColumns = cols;

    for (int i = 0; i < m_cards.size(); ++i) {
        int row = i / cols;
        int col = i % cols;
        m_cardGrid->addWidget(m_cards[i], row, col);
    }
    const int rowCount = (m_cards.size() + cols - 1) / cols;
    m_cardGrid->setRowStretch(rowCount, 1);
    m_cardContainer->updateGeometry();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_cardContainer && event->type() == QEvent::Resize) {
        if (!m_cards.isEmpty()) {
            relayoutCards();
        }
    }
    return QWidget::eventFilter(obj, event);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (windowState() & Qt::WindowMinimized) {
            if (m_trayAvailable) {
                hide();
                event->ignore();
                return;
            }
        }
    }
    QWidget::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_quitFromTray) {
        event->accept();
    } else if (m_trayAvailable) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::showWindow()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        showWindow();
    }
}

void MainWindow::trayExit()
{
    m_quitFromTray = true;
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    QApplication::quit();
}

QJsonObject MainWindow::restoreResult(const QJsonObject& account) const
{
    QString key = DataManager::accountKey(account);
    QJsonObject entry = m_cache.value(key).toObject();

    QJsonObject result;
    result["ok"] = entry.value("ok").toBool(false);
    result["message"] = entry.value("message").toString();
    result["fallback"] = entry.value("fallback").toBool(false);
    result["fallback_title"] = entry.value("fallback_title").toString();
    result["queried_at_str"] = entry.value("queried_at_str").toString();
    result["info"] = entry.value("info").toObject();
    return result;
}

QJsonObject MainWindow::accountByName(const QString& name) const
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i].toObject().value("name").toString() == name)
            return m_accounts[i].toObject();
    }
    return QJsonObject();
}

QJsonObject MainWindow::accountByKey(const QString& key) const
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (DataManager::accountKey(m_accounts[i].toObject()) == key)
            return m_accounts[i].toObject();
    }
    return QJsonObject();
}

int MainWindow::accountIndex(const QString& name) const
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i].toObject().value("name").toString() == name)
            return i;
    }
    return -1;
}

AccountCard* MainWindow::findCard(const QString& name) const
{
    for (AccountCard* card : m_cards) {
        if (card->accountName() == name)
            return card;
    }
    return nullptr;
}

void MainWindow::importAccount()
{
    ImportDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QJsonArray newAccounts = dlg.accounts();
    QSet<QString> existing;
    for (int i = 0; i < m_accounts.size(); ++i) {
        existing.insert(m_accounts[i].toObject().value("name").toString());
    }

    for (int i = 0; i < newAccounts.size(); ++i) {
        QJsonObject acc = newAccounts[i].toObject();
        if (!existing.contains(acc.value("name").toString())) {
            m_accounts.append(acc);
            existing.insert(acc.value("name").toString());
        }
    }

    m_dm->saveAccounts(m_accounts);
    sortAccounts();
    rebuildCards();

    for (int i = 0; i < newAccounts.size(); ++i) {
        checkQuota(newAccounts[i].toObject().value("name").toString());
    }
}

void MainWindow::checkAllQuotas()
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        checkQuota(m_accounts[i].toObject().value("name").toString());
    }
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_queryIntervalMinutes, m_remoteConfig, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_queryIntervalMinutes = dlg.intervalMinutes();
        m_remoteConfig = dlg.remoteConfig();
        saveState();
        rebuildCards();
    }
}

void MainWindow::reloadAccounts()
{
    m_accounts = m_dm->loadAccounts();
    m_cache = m_dm->loadCache();
    sortAccounts();
    rebuildCards();
}

void MainWindow::checkQuota(const QString& accountName)
{
    QJsonObject account = accountByName(accountName);
    if (account.isEmpty()) return;

    QString key = DataManager::accountKey(account);
    if (m_queryingKeys.contains(key)) return;

    QJsonObject credentials = account.value("credentials").toObject();
    QString accessToken = credentials.value("access_token").toString();
    if (accessToken.isEmpty()) {
        AccountCard* card = findCard(accountName);
        if (card) {
            QJsonObject err;
            err["ok"] = false;
            err["message"] = QString::fromUtf8("未找到 access_token。");
            card->setResult(err);
        }
        return;
    }

    m_queryingKeys.insert(key);
    AccountCard* card = findCard(accountName);
    if (card) card->setLoading(true);

    startUsageQuery(accountName, accessToken, key);
}

void MainWindow::startUsageQuery(const QString& name, const QString& token, const QString& key)
{
    QNetworkRequest request(QUrl("https://chatgpt.com/backend-api/wham/usage"));
    request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Origin", "https://chatgpt.com");
    request.setRawHeader("Referer", "https://chatgpt.com/");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(15000);
#endif

    QNetworkReply* reply = m_nam->get(request);
    QTimer::singleShot(18000, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, name, token, key]() {
        onUsageReplyFinished(reply, name, token, key, 0);
    });
}

void MainWindow::onUsageReplyFinished(QNetworkReply* reply, const QString& name,
                                       const QString& token, const QString& key, int stage)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError &&
        reply->error() != QNetworkReply::ContentConflictError &&
        reply->error() != QNetworkReply::ContentAccessDenied &&
        reply->error() != QNetworkReply::AuthenticationRequiredError) {
    }

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();

    if (stage == 0) {
        if (reply->error() == QNetworkReply::NoError && statusCode == 200 && !data.isEmpty()) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(data, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject info = DataManager::parseUsageResponse(doc.object(), name);
                QJsonObject result;
                result["ok"] = true;
                result["account"] = name;
                result["info"] = info;
                result["queried_at_str"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                QJsonObject account = accountByKey(key);
                if (!account.isEmpty()) cacheResult(account, result);
                m_queryingKeys.remove(key);

                AccountCard* card = findCard(name);
                if (card) card->setResult(result);

                sortAccounts();
                rebuildCards();
                return;
            }
        }

        QNetworkRequest req2(QUrl("https://api.openai.com/dashboard/billing/subscription"));
        req2.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
        req2.setRawHeader("Content-Type", "application/json");
        req2.setRawHeader("User-Agent", "Mozilla/5.0");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        req2.setTransferTimeout(10000);
#endif

        QNetworkReply* reply2 = m_nam->get(req2);
        QTimer::singleShot(12000, reply2, [reply2]() {
            if (reply2->isRunning())
                reply2->abort();
        });
        connect(reply2, &QNetworkReply::finished, this, [this, reply2, name, token, key]() {
            onUsageReplyFinished(reply2, name, token, key, 1);
        });
        return;
    }

    if (stage == 1) {
        if (reply->error() == QNetworkReply::NoError && statusCode == 200 && !data.isEmpty()) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(data, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject dobj = doc.object();
                QJsonObject planObj = dobj.value("plan").toObject();
                QString plan = planObj.value("id").toString(QString::fromUtf8("未知"));
                bool hasPm = dobj.value("has_payment_method").toBool(false);

                QJsonObject result;
                result["ok"] = true;
                result["account"] = name;
                result["fallback"] = true;
                result["fallback_title"] = QString::fromUtf8("订阅接口");
                result["message"] = QString::fromUtf8("订阅计划: %1 | 支付方式: %2")
                                        .arg(plan, hasPm ? QString::fromUtf8("已绑定") : QString::fromUtf8("未绑定"));

                QJsonObject info;
                info["account"] = name;
                info["plan"] = plan;
                info["rate_limit_allowed"] = QJsonValue::Null;
                info["rate_limit_reached"] = QJsonValue::Null;
                info["primary"] = QJsonObject();
                info["secondary"] = QJsonObject();
                result["info"] = info;
                result["queried_at_str"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

                QJsonObject account = accountByKey(key);
                if (!account.isEmpty()) cacheResult(account, result);
                m_queryingKeys.remove(key);

                AccountCard* card = findCard(name);
                if (card) card->setResult(result);

                sortAccounts();
                rebuildCards();
                return;
            }
        }

        QNetworkRequest req3(QUrl("https://api.openai.com/v1/models"));
        req3.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
        req3.setRawHeader("Content-Type", "application/json");
        req3.setRawHeader("User-Agent", "Mozilla/5.0");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        req3.setTransferTimeout(10000);
#endif

        QNetworkReply* reply3 = m_nam->get(req3);
        QTimer::singleShot(12000, reply3, [reply3]() {
            if (reply3->isRunning())
                reply3->abort();
        });
        connect(reply3, &QNetworkReply::finished, this, [this, reply3, name, token, key]() {
            onUsageReplyFinished(reply3, name, token, key, 2);
        });
        return;
    }

    QJsonObject result;
    if (stage == 2 && statusCode == 200) {
        result["ok"] = true;
        result["account"] = name;
        result["fallback"] = true;
        result["fallback_title"] = QString::fromUtf8("Token 连通性");
        result["message"] = QString::fromUtf8("Token 有效，但 wham/usage 不可用");
        result["info"] = DataManager::parseUsageResponse(QJsonObject(), name);
    } else {
        result["ok"] = false;
        result["account"] = name;
        QString reason = reply->errorString();
        if (reason.isEmpty())
            reason = QString::fromUtf8("HTTP %1").arg(statusCode);
        result["message"] = QString::fromUtf8("查询失败: %1 (HTTP %2)。如果是 SSL 错误，请确认 exe 同目录存在 libssl/libcrypto。")
                                .arg(reason, QString::number(statusCode));
    }
    result["queried_at_str"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QJsonObject account = accountByKey(key);
    if (!account.isEmpty()) cacheResult(account, result);
    m_queryingKeys.remove(key);

    AccountCard* card = findCard(name);
    if (card) card->setResult(result);

    sortAccounts();
    rebuildCards();
}

void MainWindow::cacheResult(const QJsonObject& account, const QJsonObject& result)
{
    QString key = DataManager::accountKey(account);
    QJsonObject entry;
    entry["queried_at_ts"] = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000.0;
    entry["queried_at_str"] = result.value("queried_at_str").toString(
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    entry["ok"] = result.value("ok");
    entry["message"] = result.value("message");
    entry["fallback"] = result.value("fallback");
    entry["fallback_title"] = result.value("fallback_title");
    entry["info"] = result.value("info");

    m_cache[key] = entry;
    m_dm->saveCache(m_cache);
}

double MainWindow::cacheAgeSeconds(const QJsonObject& account) const
{
    QString key = DataManager::accountKey(account);
    QJsonObject entry = m_cache.value(key).toObject();
    if (entry.isEmpty()) return -1;

    double ts = entry.value("queried_at_ts").toDouble(-1);
    if (ts < 0) return -1;

    double now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000.0;
    return now - ts;
}

void MainWindow::sortAccounts()
{
    QList<QJsonObject> accounts;
    for (int i = 0; i < m_accounts.size(); ++i)
        accounts.append(m_accounts.at(i).toObject());

    auto sortKey = [this](const QJsonObject& oa, const QJsonObject& ob) -> bool {
        QString na = oa.value("name").toString();
        QString nb = ob.value("name").toString();

        QString ka = DataManager::accountKey(oa);
        QString kb = DataManager::accountKey(ob);

        QJsonObject ea = m_cache.value(ka).toObject();
        QJsonObject eb = m_cache.value(kb).toObject();

        if (ea.isEmpty() && eb.isEmpty()) return na < nb;
        if (ea.isEmpty()) return false;
        if (eb.isEmpty()) return true;

        QJsonObject ia = ea.value("info").toObject();
        QJsonObject ib = eb.value("info").toObject();

        bool allowedA = ia.value("rate_limit_allowed").toBool(false);
        bool allowedB = ib.value("rate_limit_allowed").toBool(false);

        if (allowedA && !allowedB) return true;
        if (!allowedA && allowedB) return false;

        double resetA = ia.value("primary").toObject().value("reset_after_seconds").toDouble(-1);
        double resetB = ib.value("primary").toObject().value("reset_after_seconds").toDouble(-1);

        if (resetA >= 0 && resetB >= 0) return resetA < resetB;
        if (resetA >= 0) return true;
        if (resetB >= 0) return false;

        return na < nb;
    };

    std::sort(accounts.begin(), accounts.end(), sortKey);
    m_accounts = QJsonArray();
    for (const QJsonObject& account : accounts)
        m_accounts.append(account);
}

void MainWindow::switchAccount(const QString& accountName)
{
    QJsonObject account = accountByName(accountName);
    if (account.isEmpty()) return;

    QString errorMsg;
    if (m_dm->backupAndWriteAuthFile(account, errorMsg)) {
        QMessageBox::information(this, QString::fromUtf8("成功"),
            QString::fromUtf8("已成功切换至账号 %1。\n凭证已保存至 %2")
                .arg(accountName, m_dm->codexAuthFilePath()));
        restartLocalCodex();
    } else {
        QMessageBox::critical(this, QString::fromUtf8("错误"), errorMsg);
    }
}

void MainWindow::cloudSwitchAccount(const QString& accountName)
{
    QJsonObject account = accountByName(accountName);
    if (account.isEmpty()) return;

    QJsonObject rc = m_remoteConfig;
    QString user = rc.value("user").toString("haoze");
    QString host = rc.value("host").toString("127.0.0.1");
    QString port = QString::number(rc.value("port").toInt(9002));

    m_cloudAccountName = accountName;
    m_cloudStage = CheckAuth;

    QString tmpPath;
    QString errorMsg;
    if (!m_dm->writeTempAuthFile(account, tmpPath, errorMsg)) {
        QMessageBox::critical(this, QString::fromUtf8("远程替换失败"), errorMsg);
        return;
    }
    m_cloudTmpPath = tmpPath;

    if (m_cloudProcess) {
        m_cloudProcess->kill();
        m_cloudProcess->deleteLater();
    }

    m_cloudProcess = new QProcess(this);
    connect(m_cloudProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onRemoteAuthCheckFinished);

    QStringList args;
    args << "-o" << "ConnectTimeout=10" << "-o" << "BatchMode=yes"
         << "-o" << "StrictHostKeyChecking=no"
         << "-o" << "NumberOfPasswordPrompts=0"
         << "-T"
         << "-p" << port << user + "@" + host << "test -f ~/.codex/auth.json";

    m_cloudProcess->start("ssh", args);
}

void MainWindow::onRemoteAuthCheckFinished(int exitCode)
{
    if (exitCode != 0) {
        const QString err = QString::fromUtf8(m_cloudProcess->readAllStandardError()).trimmed();
        QMessageBox::critical(this, QString::fromUtf8("远程替换失败"),
            QString::fromUtf8("远端不存在 ~/.codex/auth.json，已停止替换。\n"
                              "程序不会自动创建 ~/.codex；请先确认远端 Codex 已初始化并存在 auth.json。\n%1")
                .arg(err.left(500)));
        cleanupCloudTemp();
        return;
    }

    m_cloudStage = Backup;
    QJsonObject rc = m_remoteConfig;
    QString user = rc.value("user").toString("haoze");
    QString host = rc.value("host").toString("127.0.0.1");
    QString port = QString::number(rc.value("port").toInt(9002));

    disconnect(m_cloudProcess, nullptr, this, nullptr);
    connect(m_cloudProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onRemoteBackupFinished);

    QString ts = QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
    QString backupCmd = QString("if [ -f ~/.codex/auth.json ]; then cp ~/.codex/auth.json ~/.codex/auth.json.bak_%1; fi").arg(ts);

    QStringList args;
    args << "-o" << "ConnectTimeout=10" << "-o" << "BatchMode=yes"
         << "-o" << "StrictHostKeyChecking=no"
         << "-o" << "NumberOfPasswordPrompts=0"
         << "-T"
         << "-p" << port << user + "@" + host << backupCmd;

    m_cloudProcess->start("ssh", args);
}

void MainWindow::onRemoteBackupFinished(int exitCode)
{
    if (exitCode != 0) {
        QMessageBox::critical(this, QString::fromUtf8("远程替换失败"),
            QString::fromUtf8("远端备份 auth.json 失败:\n%1")
                .arg(QString(m_cloudProcess->readAllStandardError())));
        cleanupCloudTemp();
        return;
    }

    m_cloudStage = Upload;
    QJsonObject rc = m_remoteConfig;
    QString user = rc.value("user").toString("haoze");
    QString host = rc.value("host").toString("127.0.0.1");
    QString port = QString::number(rc.value("port").toInt(9002));

    disconnect(m_cloudProcess, nullptr, this, nullptr);
    connect(m_cloudProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onScpUploadFinished);

    QStringList args;
    args << "-o" << "ConnectTimeout=10"
         << "-o" << "StrictHostKeyChecking=no"
         << "-P" << port
         << m_cloudTmpPath << user + "@" + host + ":~/.codex/auth.json";

    m_cloudProcess->start("scp", args);
}

void MainWindow::onScpUploadFinished(int exitCode)
{
    if (exitCode == 0) {
        restartRemoteCodex();
    } else {
        QMessageBox::critical(this, QString::fromUtf8("远程替换失败"),
            QString::fromUtf8("无法上传 auth.json 到远程:\n%1")
                .arg(QString(m_cloudProcess->readAllStandardError())));
        cleanupCloudTemp();
    }
}

void MainWindow::cleanupCloudTemp()
{
    if (!m_cloudTmpPath.isEmpty()) {
        QFile::remove(m_cloudTmpPath);
        m_cloudTmpPath.clear();
    }
}

void MainWindow::restartLocalCodex()
{
    const QString codexProgram = findCodexExecutable();
    if (codexProgram.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("Codex 重启失败"),
            QString::fromUtf8("本地 auth.json 已替换，但未找到本地 Codex 可执行文件。\n"
                              "已尝试 codex.exe/codex.cmd/codex、WindowsApps 与 npm 常见路径。"));
        return;
    }

    QProcess* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, codexProgram](int exitCode, QProcess::ExitStatus status) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0) {
            QMessageBox::information(this, QString::fromUtf8("Codex 已重启"),
                QString::fromUtf8("本地 auth.json 已替换，并已请求重启本地 Codex。"));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("Codex 重启失败"),
                QString::fromUtf8("本地 auth.json 已替换，但自动重启 Codex 失败。\n程序: %1\n%2")
                    .arg(codexProgram, err.left(500)));
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, codexProgram](QProcess::ProcessError) {
        proc->deleteLater();
        QMessageBox::warning(this, QString::fromUtf8("Codex 重启失败"),
            QString::fromUtf8("本地 auth.json 已替换，但无法启动 Codex。\n程序: %1").arg(codexProgram));
    });
    proc->start(codexProgram, QStringList() << "app-server" << "daemon" << "restart");
}

void MainWindow::restartRemoteCodex()
{
    m_cloudStage = Restart;
    QJsonObject rc = m_remoteConfig;
    QString user = rc.value("user").toString("haoze");
    QString host = rc.value("host").toString("127.0.0.1");
    QString port = QString::number(rc.value("port").toInt(9002));

    disconnect(m_cloudProcess, nullptr, this, nullptr);
    connect(m_cloudProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onRemoteRestartFinished);

    const QString restartCmd =
        "codex_bin=\"$HOME/.codex/packages/standalone/current/codex\"; "
        "if [ ! -x \"$codex_bin\" ]; then codex_bin=codex; fi; "
        "\"$codex_bin\" app-server daemon restart || { "
        "python3 - <<'PY'\n"
        "import os, signal, time\n"
        "skip={os.getpid(), os.getppid()}\n"
        "needles=['codex app-server --listen','codex app-server proxy',"
        "'/.codex/packages/standalone/current/codex app-server']\n"
        "targets=[]\n"
        "for pid in filter(str.isdigit, os.listdir('/proc')):\n"
        "    ipid=int(pid)\n"
        "    if ipid in skip: continue\n"
        "    try:\n"
        "        cmd=open('/proc/%s/cmdline'%pid,'rb').read().replace(b'\\0',b' ').decode('utf-8','ignore')\n"
        "    except Exception:\n"
        "        continue\n"
        "    if any(n in cmd for n in needles): targets.append(ipid)\n"
        "for pid in targets:\n"
        "    try: os.kill(pid, signal.SIGTERM)\n"
        "    except ProcessLookupError: pass\n"
        "time.sleep(1)\n"
        "for pid in targets:\n"
        "    try: os.kill(pid, 0)\n"
        "    except ProcessLookupError: continue\n"
        "    try: os.kill(pid, signal.SIGKILL)\n"
        "    except ProcessLookupError: pass\n"
        "PY\n"
        "\"$codex_bin\" app-server daemon bootstrap --remote-control; }";

    QStringList args;
    args << "-o" << "ConnectTimeout=10" << "-o" << "BatchMode=yes"
         << "-o" << "StrictHostKeyChecking=no"
         << "-o" << "NumberOfPasswordPrompts=0"
         << "-T"
         << "-p" << port << user + "@" + host << restartCmd;

    m_cloudProcess->start("ssh", args);
}

void MainWindow::onRemoteRestartFinished(int exitCode)
{
    if (exitCode == 0) {
        QMessageBox::information(this, QString::fromUtf8("成功"),
            QString::fromUtf8("已成功将账号 %1 的凭证推送至远程，并已请求重启远端 Codex。").arg(m_cloudAccountName));
    } else {
        QMessageBox::warning(this, QString::fromUtf8("远端 Codex 重启失败"),
            QString::fromUtf8("auth.json 已上传，但远端 Codex 重启失败:\n%1")
                .arg(QString(m_cloudProcess->readAllStandardError()).left(500)));
    }
    cleanupCloudTemp();
}

void MainWindow::deleteAccount(const QString& accountName)
{
    QJsonObject account = accountByName(accountName);
    if (account.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, QString::fromUtf8("确认删除"),
        QString::fromUtf8("确定要删除账号 %1 吗？").arg(accountName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) return;

    int idx = accountIndex(accountName);
    if (idx < 0) return;

    m_accounts.removeAt(idx);
    QString key = DataManager::accountKey(account);
    m_cache.remove(key);

    m_dm->saveCache(m_cache);
    m_dm->saveAccounts(m_accounts);
    sortAccounts();
    rebuildCards();
}

void MainWindow::autoQueryCheck()
{
    double intervalSecs = m_queryIntervalMinutes * 60.0;
    for (int i = 0; i < m_accounts.size(); ++i) {
        QJsonObject acc = m_accounts[i].toObject();
        QString key = DataManager::accountKey(acc);
        if (m_queryingKeys.contains(key)) continue;

        double age = cacheAgeSeconds(acc);
        if (age < 0 || age >= intervalSecs) {
            checkQuota(acc.value("name").toString());
        }
    }
}
