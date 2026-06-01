#include "mainwindow.h"
#include "accountcard.h"
#include "datamanager.h"
#include "importdialog.h"
#include "settingsdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QScrollBar>
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
#include <QRandomGenerator>
#include <cmath>

static double queryJitterSeconds()
{
    return static_cast<double>(QRandomGenerator::global()->bounded(1, 9));
}

static double stableJitterSeconds(const QString& key)
{
    return 1.0 + static_cast<double>(qHash(key) % 8);
}

static double stableOffsetSeconds(const QString& key, double intervalSeconds)
{
    if (intervalSeconds <= 1.0)
        return stableJitterSeconds(key);
    return 1.0 + static_cast<double>(qHash(key) % qMax(1, static_cast<int>(intervalSeconds - 1.0)));
}

static QString remoteCodexFileTarget(const QString& user, const QString& host, const QString& fileName)
{
    return user + "@" + host + ":.codex/" + fileName;
}

#ifdef Q_OS_WIN
static QString windowsRestartCodexScript()
{
    return QString::fromUtf8(
        "$ErrorActionPreference = 'Stop'; "
        "$app = Get-StartApps | Where-Object { $_.Name -eq 'Codex' -or $_.AppID -like 'OpenAI.Codex_*' } | Select-Object -First 1; "
        "if (-not $app) { throw '未找到 Codex 开始菜单入口'; } "
        "$targets = Get-Process Codex,codex -ErrorAction SilentlyContinue | Where-Object { "
        "  $_.Path -and ("
        "    $_.Path -like '*\\WindowsApps\\OpenAI.Codex_*\\app\\*' -or "
        "    $_.Path -like '*\\AppData\\Local\\OpenAI\\Codex\\bin\\*'"
        "  )"
        "}; "
        "if ($targets) { $targets | Stop-Process -Force; Start-Sleep -Milliseconds 800; } "
        "Start-Process ('shell:AppsFolder\\' + $app.AppID);"
    );
}
#else
static QString findCodexExecutable()
{
    const QStringList executableNames = {"codex", "codex.exe", "codex.cmd"};
    for (const QString& name : executableNames) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }
    return QString();
}
#endif

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
    , m_dm(new DataManager(this))
    , m_nam(new QNetworkAccessManager(this))
    , m_trayIcon(nullptr)
    , m_trayAvailable(false)
    , m_quitFromTray(false)
    , m_queryIntervalMinutes(10)
    , m_activeQueryIntervalMinutes(1)
    , m_quotaAlertThreshold(10)
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
    m_autoQueryTimer->setSingleShot(true);
    connect(m_autoQueryTimer, &QTimer::timeout, this, &MainWindow::autoQueryCheck);
    scheduleNextAutoQuery(30000);
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

    QPushButton* importCurrentBtn = new QPushButton(QString::fromUtf8("+ 当前配置"));
    importCurrentBtn->setObjectName("softButton");
    importCurrentBtn->setCursor(Qt::PointingHandCursor);
    connect(importCurrentBtn, &QPushButton::clicked, this, &MainWindow::importCurrentAccount);

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
    toolbar->addWidget(importCurrentBtn);
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
    QString installationError;
    if (m_dm->ensureAccountInstallationIds(m_accounts, installationError)) {
        if (!m_dm->saveAccounts(m_accounts)) {
            QMessageBox::warning(this, QString::fromUtf8("账号保存失败"),
                QString::fromUtf8("installation_id 已生成，但 accounts.json 保存失败。"));
        }
    } else {
        QMessageBox::warning(this, QString::fromUtf8("installation_id 初始化失败"), installationError);
    }
    m_cache = m_dm->loadCache();

    QJsonObject settings = m_dm->loadSettings();
    m_queryIntervalMinutes = settings.value("interval_minutes").toInt(10);
    if (m_queryIntervalMinutes < 1) m_queryIntervalMinutes = 10;
    m_activeQueryIntervalMinutes = settings.value("active_interval_minutes").toInt(1);
    if (m_activeQueryIntervalMinutes < 1) m_activeQueryIntervalMinutes = 1;
    m_quotaAlertThreshold = settings.value("quota_alert_threshold").toInt(10);
    m_quotaAlertThreshold = qBound(0, m_quotaAlertThreshold, 100);
    m_activeAccountKey = settings.value("active_account_key").toString();

    m_remoteConfig = settings.value("remote_config").toObject();
    if (m_remoteConfig.isEmpty()) {
        m_remoteConfig["enabled"] = false;
        m_remoteConfig["user"] = "root";
        m_remoteConfig["host"] = "127.0.0.1";
        m_remoteConfig["port"] = 22;
    }

    normalizeAutoQuerySchedule();
    keepActiveAccountFirst();
}

void MainWindow::saveState()
{
    QJsonObject settings;
    settings["interval_minutes"] = m_queryIntervalMinutes;
    settings["active_interval_minutes"] = m_activeQueryIntervalMinutes;
    settings["quota_alert_threshold"] = m_quotaAlertThreshold;
    settings["active_account_key"] = m_activeAccountKey;
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

            const bool active = key == m_activeAccountKey;
            AccountCard* card = new AccountCard(acc, remoteEnabled, active, m_cardContainer);
            connect(card, &AccountCard::queryRequested, this, &MainWindow::checkQuota);
            connect(card, &AccountCard::switchRequested, this, &MainWindow::switchAccount);
            connect(card, &AccountCard::deleteRequested, this, &MainWindow::deleteAccount);

            if (m_queryingKeys.contains(key)) {
                card->setLoading(true);
            } else if (m_cache.contains(key)) {
                const QJsonObject cacheEntry = m_cache.value(key).toObject();
                if (cacheEntry.contains("queried_at_ts") ||
                    cacheEntry.contains("message") ||
                    cacheEntry.contains("info")) {
                    card->setResult(restoreResult(acc));
                }
            }
            m_cards.append(card);
        }
    }

    relayoutCards();

    m_countBadge->setText(QString::fromUtf8("%1 个账号").arg(m_accounts.size()));
    m_fileLabel->setText(m_dm->accountsFilePath());
}

void MainWindow::rebuildCardsPreservingScroll()
{
    QScrollBar* bar = m_scrollArea ? m_scrollArea->verticalScrollBar() : nullptr;
    const int scrollValue = bar ? bar->value() : 0;

    rebuildCards();

    if (!bar)
        return;

    auto restoreScroll = [bar, scrollValue]() {
        bar->setValue(qBound(bar->minimum(), scrollValue, bar->maximum()));
    };
    restoreScroll();
    QTimer::singleShot(0, this, restoreScroll);
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
    QString installationError;
    if (!m_dm->ensureAccountInstallationIds(newAccounts, installationError)) {
        QMessageBox::critical(this, QString::fromUtf8("导入失败"),
            QString::fromUtf8("生成账号 installation_id 失败:\n%1").arg(installationError));
        return;
    }

    QSet<QString> existing;
    for (int i = 0; i < m_accounts.size(); ++i) {
        existing.insert(m_accounts[i].toObject().value("name").toString());
    }

    QStringList addedNames;
    for (int i = 0; i < newAccounts.size(); ++i) {
        QJsonObject acc = newAccounts[i].toObject();
        const QString name = acc.value("name").toString();
        if (!existing.contains(name)) {
            m_accounts.append(acc);
            existing.insert(name);
            addedNames.append(name);
        }
    }

    if (!m_dm->saveAccounts(m_accounts)) {
        QMessageBox::warning(this, QString::fromUtf8("账号保存失败"),
            QString::fromUtf8("导入账号已处理，但 accounts.json 保存失败。"));
    }
    sortAccounts();
    rebuildCards();

    for (const QString& name : addedNames)
        checkQuota(name);
}

void MainWindow::importCurrentAccount()
{
    QJsonObject account;
    QString errorMsg;
    if (!m_dm->importCurrentAuthAccount(account, errorMsg)) {
        QMessageBox::critical(this, QString::fromUtf8("导入当前配置失败"), errorMsg);
        return;
    }

    const QString name = account.value("name").toString();
    const QString key = DataManager::accountKey(account);
    for (int i = 0; i < m_accounts.size(); ++i) {
        QJsonObject existing = m_accounts.at(i).toObject();
        if (existing.value("name").toString() == name || DataManager::accountKey(existing) == key) {
            QString cleanupError;
            m_dm->removeAccountData(account, cleanupError);
            QMessageBox::information(this, QString::fromUtf8("已存在"),
                QString::fromUtf8("当前配置已经在账号列表中。"));
            return;
        }
    }

    m_accounts.append(account);
    if (!m_dm->saveAccounts(m_accounts)) {
        QMessageBox::warning(this, QString::fromUtf8("账号保存失败"),
            QString::fromUtf8("当前配置已复制到 data，但 accounts.json 保存失败。"));
        return;
    }

    sortAccounts();
    rebuildCards();
    checkQuota(name);
}

void MainWindow::checkAllQuotas()
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        checkQuota(m_accounts[i].toObject().value("name").toString());
    }
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_queryIntervalMinutes, m_activeQueryIntervalMinutes,
                       m_quotaAlertThreshold, m_remoteConfig, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_queryIntervalMinutes = dlg.intervalMinutes();
        m_activeQueryIntervalMinutes = dlg.activeIntervalMinutes();
        const int oldQuotaAlertThreshold = m_quotaAlertThreshold;
        m_quotaAlertThreshold = dlg.quotaAlertThreshold();
        if (m_quotaAlertThreshold != oldQuotaAlertThreshold)
            m_quotaAlertedKeys.clear();
        m_remoteConfig = dlg.remoteConfig();
        if (!m_activeAccountKey.isEmpty()) {
            QJsonObject entry = m_cache.value(m_activeAccountKey).toObject();
            const double queriedAt = entry.value("queried_at_ts").toDouble(-1);
            const bool disabled = entry.value("auto_query_disabled").toBool(false) ||
                entry.value("http_status").toInt(-1) == 401 ||
                entry.value("message").toString().contains("HTTP 401");
            if (queriedAt >= 0 && !disabled) {
                entry["next_auto_query_ts"] = queriedAt + (m_activeQueryIntervalMinutes * 60.0) + queryJitterSeconds();
                m_cache[m_activeAccountKey] = entry;
                m_dm->saveCache(m_cache);
            }
        }
        saveState();
        normalizeAutoQuerySchedule();
        rebuildCards();
        scheduleNextAutoQuery();
    }
}

void MainWindow::reloadAccounts()
{
    m_accounts = m_dm->loadAccounts();
    QString installationError;
    if (m_dm->ensureAccountInstallationIds(m_accounts, installationError)) {
        if (!m_dm->saveAccounts(m_accounts)) {
            QMessageBox::warning(this, QString::fromUtf8("账号保存失败"),
                QString::fromUtf8("installation_id 已生成，但 accounts.json 保存失败。"));
        }
    } else {
        QMessageBox::warning(this, QString::fromUtf8("installation_id 初始化失败"), installationError);
    }
    m_cache = m_dm->loadCache();
    normalizeAutoQuerySchedule();
    sortAccounts();
    rebuildCards();
    scheduleNextAutoQuery();
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
                rebuildCardsPreservingScroll();
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
                rebuildCardsPreservingScroll();
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
        result["http_status"] = statusCode;
        QString reason = reply->errorString();
        if (reason.isEmpty())
            reason = QString::fromUtf8("HTTP %1").arg(statusCode);
        QString message = QString::fromUtf8("查询失败: %1 (HTTP %2)。如果是 SSL 错误，请确认 exe 同目录存在 libssl/libcrypto。")
                              .arg(reason, QString::number(statusCode));
        if (statusCode == 401)
            message += QString::fromUtf8("\n已停止该账号的后台自动查询；手动查询仍可使用。");
        result["message"] = message;
    }
    result["queried_at_str"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    m_queryingKeys.remove(key);

    AccountCard* card = findCard(name);
    if (card) card->setResult(result);

    QJsonObject account = accountByKey(key);
    if (!account.isEmpty()) {
        cacheResult(account, result);
        sortAccounts();
        rebuildCardsPreservingScroll();
    } else {
        scheduleNextAutoQuery(30000);
    }
}

void MainWindow::cacheResult(const QJsonObject& account, const QJsonObject& result)
{
    QString key = DataManager::accountKey(account);
    const QJsonObject previous = m_cache.value(key).toObject();
    QJsonObject entry;
    const double queriedAtTs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000.0;
    const int intervalMinutes = (key == m_activeAccountKey)
        ? m_activeQueryIntervalMinutes
        : m_queryIntervalMinutes;
    const bool isUnauthorized = !result.value("ok").toBool() &&
        result.value("http_status").toInt(-1) == 401;
    entry["queried_at_ts"] = queriedAtTs;
    if (isUnauthorized) {
        entry["auto_query_disabled"] = true;
        entry["next_auto_query_ts"] = QJsonValue::Null;
    } else {
        entry["auto_query_disabled"] = false;
        entry["next_auto_query_ts"] = queriedAtTs + (intervalMinutes * 60.0) + queryJitterSeconds();
    }
    entry["queried_at_str"] = result.value("queried_at_str").toString(
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    entry["ok"] = result.value("ok");
    entry["http_status"] = result.value("http_status");
    entry["message"] = result.value("message");
    entry["fallback"] = result.value("fallback");
    entry["fallback_title"] = result.value("fallback_title");
    QJsonValue info = result.value("info");
    if (!result.value("ok").toBool() && (!info.isObject() || info.toObject().isEmpty()))
        info = previous.value("info");
    entry["info"] = info;

    m_cache[key] = entry;
    m_dm->saveCache(m_cache);
    maybeShowQuotaAlert(account, result);
    scheduleNextAutoQuery();
}

void MainWindow::maybeShowQuotaAlert(const QJsonObject& account, const QJsonObject& result)
{
    if (m_quotaAlertThreshold <= 0 || !result.value("ok").toBool())
        return;

    const QJsonObject info = result.value("info").toObject();
    if (info.isEmpty())
        return;

    const QString accountKey = DataManager::accountKey(account);
    const QString accountName = account.value("name").toString(result.value("account").toString());
    QStringList lines;
    QStringList newlyAlerted;

    auto checkWindow = [&](const QString& id, const QString& label, const QJsonObject& limit) {
        const double remaining = limit.value("remaining_pct").toDouble(-1);
        const QString alertKey = accountKey + ":" + id;
        if (remaining < 0)
            return;

        if (remaining <= m_quotaAlertThreshold) {
            if (!m_quotaAlertedKeys.contains(alertKey)) {
                lines << QString::fromUtf8("%1 仅剩 %2%，已低于提醒阈值 %3%。")
                    .arg(label)
                    .arg(QString::number(remaining, 'f', 1))
                    .arg(m_quotaAlertThreshold);
                newlyAlerted << alertKey;
            }
        } else {
            m_quotaAlertedKeys.remove(alertKey);
        }
    };

    checkWindow("primary", QString::fromUtf8("5h 限额"), info.value("primary").toObject());
    checkWindow("secondary", QString::fromUtf8("周限额"), info.value("secondary").toObject());

    if (lines.isEmpty())
        return;

    for (const QString& key : newlyAlerted)
        m_quotaAlertedKeys.insert(key);

    QMessageBox::warning(this,
        QString::fromUtf8("配额接近临界"),
        QString::fromUtf8("账号：%1\n\n%2").arg(accountName, lines.join("\n")));
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

void MainWindow::normalizeAutoQuerySchedule()
{
    const double now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000.0;
    bool changed = false;

    for (int i = 0; i < m_accounts.size(); ++i) {
        const QJsonObject account = m_accounts.at(i).toObject();
        const QString key = DataManager::accountKey(account);
        if (key.isEmpty())
            continue;

        QJsonObject entry = m_cache.value(key).toObject();
        const bool disabled = entry.value("auto_query_disabled").toBool(false) ||
            entry.value("http_status").toInt(-1) == 401 ||
            entry.value("message").toString().contains("HTTP 401");
        if (disabled)
            continue;

        const int intervalMinutes = (key == m_activeAccountKey)
            ? m_activeQueryIntervalMinutes
            : m_queryIntervalMinutes;
        const double intervalSeconds = qMax(1, intervalMinutes) * 60.0;
        const QJsonValue nextValue = entry.value("next_auto_query_ts");
        double nextTs = nextValue.isDouble() ? nextValue.toDouble(-1) : -1;

        if (nextTs < 0) {
            const double queriedAt = entry.value("queried_at_ts").toDouble(-1);
            if (queriedAt >= 0) {
                nextTs = queriedAt + intervalSeconds + stableJitterSeconds(key);
            } else {
                nextTs = now + stableOffsetSeconds(key, intervalSeconds);
            }
        }

        if (nextTs <= now) {
            const double missedIntervals = std::floor((now - nextTs) / intervalSeconds) + 1.0;
            nextTs += missedIntervals * intervalSeconds;
        }

        if (!nextValue.isDouble() || qAbs(nextValue.toDouble(-1) - nextTs) > 0.001) {
            entry["next_auto_query_ts"] = nextTs;
            entry["auto_query_disabled"] = false;
            m_cache[key] = entry;
            changed = true;
        }
    }

    if (changed)
        m_dm->saveCache(m_cache);
}

qint64 MainWindow::nextAutoQueryAtMs(const QJsonObject& account) const
{
    const QString key = DataManager::accountKey(account);
    const QJsonObject entry = m_cache.value(key).toObject();
    if (entry.isEmpty())
        return -1;
    if (entry.value("auto_query_disabled").toBool(false) ||
        entry.value("http_status").toInt(-1) == 401 ||
        entry.value("message").toString().contains("HTTP 401")) {
        return -1;
    }

    const int intervalMinutes = (key == m_activeAccountKey)
        ? m_activeQueryIntervalMinutes
        : m_queryIntervalMinutes;
    const double intervalSeconds = qMax(1, intervalMinutes) * 60.0;
    const QJsonValue nextValue = entry.value("next_auto_query_ts");
    double nextTs = nextValue.isDouble() ? nextValue.toDouble(-1) : -1;
    if (nextTs < 0) {
        const double queriedAt = entry.value("queried_at_ts").toDouble(-1);
        if (queriedAt < 0)
            return -1;
        nextTs = queriedAt + intervalSeconds + stableJitterSeconds(key);
    }
    return static_cast<qint64>(nextTs * 1000.0);
}

void MainWindow::scheduleNextAutoQuery(int minimumDelayMs)
{
    if (!m_autoQueryTimer)
        return;

    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    qint64 nextMs = -1;
    for (int i = 0; i < m_accounts.size(); ++i) {
        const qint64 due = nextAutoQueryAtMs(m_accounts.at(i).toObject());
        if (due < 0)
            continue;
        if (nextMs < 0 || due < nextMs)
            nextMs = due;
    }

    int delayMs = 30000;
    if (nextMs >= 0) {
        const qint64 untilDue = qMax<qint64>(0, nextMs - nowMs);
        delayMs = static_cast<int>(qMin<qint64>(30000, untilDue));
    }
    delayMs = qMax(delayMs, minimumDelayMs);
    m_autoQueryTimer->start(delayMs);
}

void MainWindow::refreshCardTimes()
{
    for (AccountCard* card : m_cards)
        card->refreshRelativeTimes();
}

void MainWindow::sortAccounts()
{
    QList<QJsonObject> accounts;
    for (int i = 0; i < m_accounts.size(); ++i)
        accounts.append(m_accounts.at(i).toObject());

    auto sortKey = [this](const QJsonObject& oa, const QJsonObject& ob) -> bool {
        struct Rank {
            int bucket = 4;
            double primaryRemaining = -1;
            double secondaryRemaining = -1;
            double primaryResetSeconds = 1.0e12;
            double secondaryResetSeconds = 1.0e12;
            QString name;
            QString key;
        };

        auto resetSeconds = [](const QJsonObject& limit) {
            const double resetAt = limit.value("reset_at_dt").toDouble(0);
            if (resetAt > 0) {
                const double now = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
                return qMax(0.0, resetAt - now);
            }

            const double resetAfter = limit.value("reset_after_seconds").toDouble(-1);
            return resetAfter >= 0 ? resetAfter : 1.0e12;
        };

        auto rankFor = [this, resetSeconds](const QJsonObject& account) {
            Rank rank;
            rank.name = account.value("name").toString();

            const QString key = DataManager::accountKey(account);
            rank.key = key;
            if (!m_activeAccountKey.isEmpty() && key == m_activeAccountKey) {
                rank.bucket = -1;
                return rank;
            }

            const QJsonObject entry = m_cache.value(key).toObject();
            if (entry.isEmpty() || !entry.value("ok").toBool(false))
                return rank;

            const QJsonObject info = entry.value("info").toObject();
            const QJsonObject primary = info.value("primary").toObject();
            const QJsonObject secondary = info.value("secondary").toObject();
            rank.primaryRemaining = primary.value("remaining_pct").toDouble(-1);
            rank.secondaryRemaining = secondary.value("remaining_pct").toDouble(-1);
            rank.primaryResetSeconds = resetSeconds(primary);
            rank.secondaryResetSeconds = resetSeconds(secondary);

            const bool weeklyEmpty = rank.secondaryRemaining >= 0 && rank.secondaryRemaining <= 0.5;
            const bool primaryEmpty = rank.primaryRemaining >= 0 && rank.primaryRemaining <= 0.5;
            if (weeklyEmpty) {
                rank.bucket = 3;
            } else if (primaryEmpty) {
                rank.bucket = 2;
            } else if (rank.primaryRemaining >= 0 && rank.secondaryRemaining >= 0) {
                rank.bucket = 0;
            }
            return rank;
        };

        const Rank a = rankFor(oa);
        const Rank b = rankFor(ob);

        if (a.bucket != b.bucket)
            return a.bucket < b.bucket;

        if (a.bucket == 2) {
            if (a.primaryResetSeconds != b.primaryResetSeconds)
                return a.primaryResetSeconds < b.primaryResetSeconds;
        } else if (a.bucket == 3) {
            if (a.secondaryResetSeconds != b.secondaryResetSeconds)
                return a.secondaryResetSeconds < b.secondaryResetSeconds;
        } else if (a.bucket == 0) {
            const bool aPrimaryFull = a.primaryRemaining >= 99.5;
            const bool bPrimaryFull = b.primaryRemaining >= 99.5;
            if (aPrimaryFull != bPrimaryFull)
                return aPrimaryFull;
            if (aPrimaryFull && bPrimaryFull && a.secondaryRemaining != b.secondaryRemaining)
                return a.secondaryRemaining > b.secondaryRemaining;
            if (a.primaryRemaining != b.primaryRemaining)
                return a.primaryRemaining > b.primaryRemaining;
            if (a.secondaryRemaining != b.secondaryRemaining)
                return a.secondaryRemaining > b.secondaryRemaining;
        }

        return a.name < b.name;
    };

    std::sort(accounts.begin(), accounts.end(), sortKey);
    m_accounts = QJsonArray();
    for (const QJsonObject& account : accounts)
        m_accounts.append(account);
    m_dm->saveAccounts(m_accounts);
}

void MainWindow::keepActiveAccountFirst()
{
    if (m_activeAccountKey.isEmpty() || m_accounts.size() < 2)
        return;

    int activeIndex = -1;
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (DataManager::accountKey(m_accounts.at(i).toObject()) == m_activeAccountKey) {
            activeIndex = i;
            break;
        }
    }
    if (activeIndex <= 0)
        return;

    const QJsonValue activeAccount = m_accounts.at(activeIndex);
    m_accounts.removeAt(activeIndex);
    m_accounts.insert(0, activeAccount);
    m_dm->saveAccounts(m_accounts);
}

void MainWindow::switchAccount(const QString& accountName)
{
    QJsonObject account = accountByName(accountName);
    if (account.isEmpty()) return;
    if (m_switchInProgress) {
        QMessageBox::information(this, QString::fromUtf8("切换进行中"),
            QString::fromUtf8("已有账号切换任务正在执行，请稍后再试。"));
        return;
    }

    const bool remoteEnabled = m_remoteConfig.value("enabled").toBool(false);
    m_switchInProgress = true;
    m_switchAccountName = accountName;
    m_waitingLocalRestart = true;
    m_waitingCloudSwitch = false;
    m_localSwitchOk = false;
    m_cloudSwitchOk = !remoteEnabled;
    m_localSwitchMessage.clear();
    m_cloudSwitchMessage.clear();

    QString errorMsg;
    if (!m_dm->backupAndWriteAuthFile(account, errorMsg)) {
        if (remoteEnabled) {
            m_cloudSwitchOk = false;
            m_cloudSwitchMessage = QString::fromUtf8("云端未执行: 本地替换失败。");
        }
        finishLocalRestart(false, QString::fromUtf8("本地替换失败: %1").arg(errorMsg));
        return;
    }

    m_activeAccountKey = DataManager::accountKey(account);
    QJsonObject activeEntry = m_cache.value(m_activeAccountKey).toObject();
    const double nowTs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000.0;
    const bool disabled = activeEntry.value("auto_query_disabled").toBool(false) ||
        activeEntry.value("http_status").toInt(-1) == 401 ||
        activeEntry.value("message").toString().contains("HTTP 401");
    if (!disabled)
        activeEntry["next_auto_query_ts"] = nowTs + (m_activeQueryIntervalMinutes * 60.0) + queryJitterSeconds();
    m_cache[m_activeAccountKey] = activeEntry;
    m_dm->saveCache(m_cache);
    saveState();
    sortAccounts();
    rebuildCards();
    scheduleNextAutoQuery(disabled ? 30000 : m_activeQueryIntervalMinutes * 60 * 1000);

    if (remoteEnabled) {
        m_waitingCloudSwitch = true;
        startCloudSwitch(accountName);
    }
    restartLocalCodex();
}

void MainWindow::startCloudSwitch(const QString& accountName)
{
    QJsonObject account = accountByName(accountName);
    if (account.isEmpty()) {
        finishCloudSwitch(false, QString::fromUtf8("云端替换失败: 未找到账号。"));
        return;
    }

    QJsonObject rc = m_remoteConfig;
    QString user = rc.value("user").toString("root");
    QString host = rc.value("host").toString("127.0.0.1");
    QString port = QString::number(rc.value("port").toInt(22));

    m_cloudAccountName = accountName;
    m_cloudStage = CheckAuth;

    QString tmpPath;
    QString errorMsg;
    if (!m_dm->writeTempCurrentAuthFile(tmpPath, errorMsg)) {
        finishCloudSwitch(false, QString::fromUtf8("云端替换失败: %1").arg(errorMsg));
        return;
    }
    m_cloudTmpPath = tmpPath;

    QString tmpInstallationPath;
    if (!m_dm->writeTempCurrentInstallationIdFile(tmpInstallationPath, errorMsg)) {
        cleanupCloudTemp();
        finishCloudSwitch(false, QString::fromUtf8("云端替换失败: %1").arg(errorMsg));
        return;
    }
    m_cloudInstallationTmpPath = tmpInstallationPath;

    if (m_cloudProcess) {
        m_cloudProcess->kill();
        m_cloudProcess->deleteLater();
    }

    m_cloudProcess = new QProcess(this);
    connect(m_cloudProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        finishCloudSwitch(false,
            QString::fromUtf8("云端替换失败: 无法启动 ssh/scp 进程。\n%1")
                .arg(m_cloudProcess ? m_cloudProcess->errorString() : QString()));
    });
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
        cleanupCloudTemp();
        finishCloudSwitch(false,
            QString::fromUtf8("云端替换失败: 远端不存在 ~/.codex/auth.json，已停止替换。\n"
                              "程序不会自动创建 ~/.codex；请先确认远端 Codex 已初始化并存在 auth.json。\n%1")
                .arg(err.left(500)));
        return;
    }

    m_cloudStage = Backup;
    QJsonObject rc = m_remoteConfig;
    QString user = rc.value("user").toString("root");
    QString host = rc.value("host").toString("127.0.0.1");
    QString port = QString::number(rc.value("port").toInt(22));

    disconnect(m_cloudProcess, nullptr, this, nullptr);
    connect(m_cloudProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        finishCloudSwitch(false,
            QString::fromUtf8("云端替换失败: 无法启动 ssh/scp 进程。\n%1")
                .arg(m_cloudProcess ? m_cloudProcess->errorString() : QString()));
    });
    connect(m_cloudProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onRemoteBackupFinished);

    QString ts = QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
    QString backupCmd = QString(
        "if [ -f ~/.codex/auth.json ]; then cp ~/.codex/auth.json ~/.codex/auth.json.bak_%1; fi; "
        "if [ -f ~/.codex/installation_id ]; then cp ~/.codex/installation_id ~/.codex/installation_id.bak_%1; fi"
    ).arg(ts);

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
        const QString err = QString::fromUtf8(m_cloudProcess->readAllStandardError());
        cleanupCloudTemp();
        finishCloudSwitch(false, QString::fromUtf8("云端替换失败: 远端备份 auth.json 失败:\n%1").arg(err));
        return;
    }

    m_cloudStage = UploadAuth;
    QJsonObject rc = m_remoteConfig;
    QString user = rc.value("user").toString("root");
    QString host = rc.value("host").toString("127.0.0.1");
    QString port = QString::number(rc.value("port").toInt(22));

    disconnect(m_cloudProcess, nullptr, this, nullptr);
    connect(m_cloudProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        finishCloudSwitch(false,
            QString::fromUtf8("云端替换失败: 无法启动 ssh/scp 进程。\n%1")
                .arg(m_cloudProcess ? m_cloudProcess->errorString() : QString()));
    });
    connect(m_cloudProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onScpAuthUploadFinished);

    QStringList args;
    args << "-o" << "ConnectTimeout=10"
         << "-o" << "BatchMode=yes"
         << "-o" << "StrictHostKeyChecking=no"
         << "-o" << "NumberOfPasswordPrompts=0"
         << "-P" << port
         << m_cloudTmpPath << remoteCodexFileTarget(user, host, "auth.json");

    m_cloudProcess->start("scp", args);
}

void MainWindow::onScpAuthUploadFinished(int exitCode)
{
    if (exitCode == 0) {
        m_cloudStage = UploadInstallationId;
        QJsonObject rc = m_remoteConfig;
        QString user = rc.value("user").toString("root");
        QString host = rc.value("host").toString("127.0.0.1");
        QString port = QString::number(rc.value("port").toInt(22));

        disconnect(m_cloudProcess, nullptr, this, nullptr);
        connect(m_cloudProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            finishCloudSwitch(false,
                QString::fromUtf8("云端替换失败: 无法启动 ssh/scp 进程。\n%1")
                    .arg(m_cloudProcess ? m_cloudProcess->errorString() : QString()));
        });
        connect(m_cloudProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &MainWindow::onScpInstallationIdUploadFinished);

        QStringList args;
        args << "-o" << "ConnectTimeout=10"
             << "-o" << "BatchMode=yes"
             << "-o" << "StrictHostKeyChecking=no"
             << "-o" << "NumberOfPasswordPrompts=0"
             << "-P" << port
             << m_cloudInstallationTmpPath << remoteCodexFileTarget(user, host, "installation_id");

        m_cloudProcess->start("scp", args);
    } else {
        const QString err = QString::fromUtf8(m_cloudProcess->readAllStandardError());
        cleanupCloudTemp();
        finishCloudSwitch(false, QString::fromUtf8("云端替换失败: 无法上传 auth.json 到远程:\n%1").arg(err));
    }
}

void MainWindow::onScpInstallationIdUploadFinished(int exitCode)
{
    if (exitCode == 0) {
        restartRemoteCodex();
    } else {
        const QString err = QString::fromUtf8(m_cloudProcess->readAllStandardError());
        cleanupCloudTemp();
        finishCloudSwitch(false, QString::fromUtf8("云端替换失败: 无法上传 installation_id 到远程:\n%1").arg(err));
    }
}

void MainWindow::cleanupCloudTemp()
{
    if (!m_cloudTmpPath.isEmpty()) {
        QFile::remove(m_cloudTmpPath);
        m_cloudTmpPath.clear();
    }
    if (!m_cloudInstallationTmpPath.isEmpty()) {
        QFile::remove(m_cloudInstallationTmpPath);
        m_cloudInstallationTmpPath.clear();
    }
}

void MainWindow::restartLocalCodex()
{
#ifdef Q_OS_WIN
    QProcess* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus status) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0) {
            finishLocalRestart(true, QString::fromUtf8("本地替换成功，并已请求重启本地 Codex。"));
        } else {
            finishLocalRestart(false,
                QString::fromUtf8("本地 auth.json 与 installation_id 已替换，但自动重启 Codex 失败。\n%1")
                    .arg(err.left(500)));
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        proc->deleteLater();
        finishLocalRestart(false,
            QString::fromUtf8("本地 auth.json 与 installation_id 已替换，但无法启动 PowerShell 重启 Codex。"));
    });
    proc->start("powershell", QStringList()
        << "-NoProfile" << "-ExecutionPolicy" << "Bypass"
        << "-Command" << windowsRestartCodexScript());
#else
    const QString codexProgram = findCodexExecutable();
    if (codexProgram.isEmpty()) {
        finishLocalRestart(false,
            QString::fromUtf8("本地 auth.json 已替换，但未找到本地 Codex 可执行文件。\n"
                              "已尝试 LocalAppData/OpenAI/Codex/bin、codex.exe/codex.cmd/codex 与 npm 常见路径。"));
        return;
    }

    QProcess* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, codexProgram](int exitCode, QProcess::ExitStatus status) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0) {
            finishLocalRestart(true, QString::fromUtf8("本地替换成功，并已请求重启本地 Codex。"));
        } else {
            finishLocalRestart(false,
                QString::fromUtf8("本地 auth.json 已替换，但自动重启 Codex 失败。\n程序: %1\n%2")
                    .arg(codexProgram, err.left(500)));
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, codexProgram](QProcess::ProcessError) {
        proc->deleteLater();
        finishLocalRestart(false,
            QString::fromUtf8("本地 auth.json 已替换，但无法启动 Codex。\n程序: %1").arg(codexProgram));
    });
    proc->start(codexProgram, QStringList() << "app-server" << "daemon" << "restart");
#endif
}

void MainWindow::restartRemoteCodex()
{
    m_cloudStage = Restart;
    QJsonObject rc = m_remoteConfig;
    QString user = rc.value("user").toString("root");
    QString host = rc.value("host").toString("127.0.0.1");
    QString port = QString::number(rc.value("port").toInt(22));

    disconnect(m_cloudProcess, nullptr, this, nullptr);
    connect(m_cloudProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        finishCloudSwitch(false,
            QString::fromUtf8("云端替换失败: 无法启动 ssh/scp 进程。\n%1")
                .arg(m_cloudProcess ? m_cloudProcess->errorString() : QString()));
    });
    connect(m_cloudProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onRemoteRestartFinished);

    const QString restartCmd =
        "chmod 600 ~/.codex/auth.json ~/.codex/installation_id 2>/dev/null || true; "
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
        finishCloudSwitch(true,
            QString::fromUtf8("云端替换成功，并已请求重启远端 Codex。"));
    } else {
        finishCloudSwitch(false,
            QString::fromUtf8("云端 auth.json 与 installation_id 已上传，但远端 Codex 重启失败:\n%1")
                .arg(QString::fromUtf8(m_cloudProcess->readAllStandardError()).left(500)));
    }
}

void MainWindow::finishLocalRestart(bool ok, const QString& message)
{
    m_localSwitchOk = ok;
    m_localSwitchMessage = message;
    m_waitingLocalRestart = false;
    maybeShowSwitchSummary();
}

void MainWindow::finishCloudSwitch(bool ok, const QString& message)
{
    cleanupCloudTemp();
    m_cloudSwitchOk = ok;
    m_cloudSwitchMessage = message;
    m_waitingCloudSwitch = false;
    maybeShowSwitchSummary();
}

void MainWindow::maybeShowSwitchSummary()
{
    if (!m_switchInProgress || m_waitingLocalRestart || m_waitingCloudSwitch)
        return;

    const bool remoteEnabledForThisSwitch = !m_cloudSwitchMessage.isEmpty();
    const bool allOk = m_localSwitchOk && m_cloudSwitchOk;

    QStringList lines;
    lines << QString::fromUtf8("账号: %1").arg(m_switchAccountName);
    lines << QString();
    lines << QString::fromUtf8("本地: %1").arg(m_localSwitchMessage);
    if (remoteEnabledForThisSwitch)
        lines << QString::fromUtf8("云端: %1").arg(m_cloudSwitchMessage);

    QMessageBox::Icon icon = allOk ? QMessageBox::Information : QMessageBox::Warning;
    QMessageBox box(icon,
        allOk ? QString::fromUtf8("切换完成") : QString::fromUtf8("切换结果"),
        lines.join("\n"),
        QMessageBox::Ok,
        this);
    box.exec();

    m_switchInProgress = false;
    m_waitingLocalRestart = false;
    m_waitingCloudSwitch = false;
    m_localSwitchOk = false;
    m_cloudSwitchOk = false;
    m_switchAccountName.clear();
    m_localSwitchMessage.clear();
    m_cloudSwitchMessage.clear();
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
    if (m_activeAccountKey == key) {
        m_activeAccountKey.clear();
        saveState();
    }

    QString dataError;
    if (!m_dm->removeAccountData(account, dataError)) {
        QMessageBox::warning(this, QString::fromUtf8("账号数据清理失败"), dataError);
    }
    m_dm->saveCache(m_cache);
    m_dm->saveAccounts(m_accounts);
    sortAccounts();
    rebuildCards();
}

void MainWindow::autoQueryCheck()
{
    refreshCardTimes();

    if (m_accounts.isEmpty() || !m_queryingKeys.isEmpty()) {
        scheduleNextAutoQuery(30000);
        return;
    }

    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    int dueIndex = -1;
    qint64 dueAt = -1;
    for (int i = 0; i < m_accounts.size(); ++i) {
        const qint64 nextAt = nextAutoQueryAtMs(m_accounts.at(i).toObject());
        if (nextAt < 0 || nextAt > nowMs)
            continue;
        if (dueIndex < 0 || nextAt < dueAt) {
            dueIndex = i;
            dueAt = nextAt;
        }
    }

    if (dueIndex >= 0) {
        checkQuota(m_accounts.at(dueIndex).toObject().value("name").toString());
        scheduleNextAutoQuery(30000);
        return;
    }

    scheduleNextAutoQuery();
}
