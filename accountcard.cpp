#include "accountcard.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>
#include <climits>

AccountCard::AccountCard(const QJsonObject& account, bool remoteEnabled, bool active, QWidget* parent)
    : QFrame(parent), m_account(account), m_isActive(active)
{
    m_accountName = account.value("name").toString("未知账号");
    setObjectName("accountCard");
    setMinimumSize(640, 272);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    const QJsonObject credentials = account.value("credentials").toObject();
    const QString email = !account.value("email").toString().isEmpty()
        ? account.value("email").toString()
        : credentials.value("email").toString();
    const QString displayName = (!email.isEmpty() && email.compare(m_accountName, Qt::CaseInsensitive) != 0)
        ? m_accountName + " · " + email
        : m_accountName;

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(11);

    auto* header = new QHBoxLayout();
    header->setSpacing(8);
    auto* name = new QLabel(displayName);
    name->setObjectName("accountName");
    name->setToolTip(displayName);
    name->setTextFormat(Qt::PlainText);
    header->addWidget(name, 1);

    m_planBadge = new QLabel(account.value("type").toString("OAUTH").toUpper());
    m_planBadge->setObjectName("badge");
    m_statusBadge = new QLabel("未查询");
    m_statusBadge->setObjectName("statusBadge");
    applyCurrentStatus();
    header->addWidget(m_planBadge);
    header->addWidget(m_statusBadge);
    root->addLayout(header);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(22);
    grid->setVerticalSpacing(8);

    auto makeBar = [](const QString& title, QLabel** percent, QLabel** meta, QProgressBar** bar) {
        auto* box = new QWidget();
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);
        auto* top = new QHBoxLayout();
        top->setSpacing(6);
        auto* titleLabel = new QLabel(title);
        titleLabel->setObjectName("barTitle");
        *percent = new QLabel("未查询");
        (*percent)->setObjectName("barPercent");
        top->addWidget(titleLabel);
        top->addStretch();
        top->addWidget(*percent);
        *bar = new QProgressBar();
        (*bar)->setRange(0, 100);
        (*bar)->setValue(0);
        (*bar)->setTextVisible(false);
        (*bar)->setFixedHeight(10);
        (*bar)->setObjectName("usageProgress");
        (*bar)->setProperty("level", QVariant("unknown"));
        *meta = new QLabel("");
        (*meta)->setObjectName("mutedText");
        (*meta)->setWordWrap(true);
        layout->addLayout(top);
        layout->addWidget(*bar);
        layout->addWidget(*meta);
        return box;
    };

    grid->addWidget(makeBar("5h 限额", &m_primaryPercent, &m_primaryMeta, &m_primaryBar), 0, 0);
    grid->addWidget(makeBar("周限额", &m_secondaryPercent, &m_secondaryMeta, &m_secondaryBar), 0, 1);
    root->addLayout(grid);

    m_message = new QLabel("查询: 未查询");
    m_message->setObjectName("cardMessage");
    m_message->setWordWrap(true);
    m_message->setMinimumHeight(38);
    root->addWidget(m_message);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(9);
    actions->addStretch();
    m_queryButton = new QPushButton("↻ 查询");
    m_queryButton->setObjectName("primaryButton");
    connect(m_queryButton, &QPushButton::clicked, this, [this]() { emit queryRequested(m_accountName); });
    auto* switchButton = new QPushButton(remoteEnabled ? "⇄ 切换" : "⇄ 本地");
    switchButton->setObjectName("softButton");
    connect(switchButton, &QPushButton::clicked, this, [this]() { emit switchRequested(m_accountName); });
    actions->addWidget(m_queryButton);
    actions->addWidget(switchButton);
    auto* deleteButton = new QPushButton("× 删除");
    deleteButton->setObjectName("dangerButton");
    connect(deleteButton, &QPushButton::clicked, this, [this]() { emit deleteRequested(m_accountName); });
    actions->addWidget(deleteButton);
    root->addLayout(actions);
}

QString AccountCard::accountName() const { return m_accountName; }
QJsonObject AccountCard::account() const { return m_account; }

void AccountCard::setActive(bool active)
{
    if (m_isActive == active)
        return;
    m_isActive = active;
    applyCurrentStatus();
}

void AccountCard::setLoading(bool loading)
{
    m_queryButton->setEnabled(!loading);
    m_queryButton->setText(loading ? "查询中..." : "↻ 查询");
    if (loading) {
        m_statusBadge->setText("查询中");
        refreshStatusStyle("loading");
    } else {
        applyCurrentStatus();
    }
}

void AccountCard::setResult(const QJsonObject& result)
{
    m_queryButton->setEnabled(true);
    m_queryButton->setText("↻ 查询");

    if (!result.value("ok").toBool()) {
        m_lastResult = result;
        const QJsonObject info = result.value("info").toObject();
        if (!info.isEmpty()) {
            const QString plan = info.value("plan").toString();
            if (!plan.isEmpty())
                m_planBadge->setText(plan.toUpper());
            setLimit(m_primaryBar, m_primaryPercent, m_primaryMeta, info.value("primary").toObject());
            setLimit(m_secondaryBar, m_secondaryPercent, m_secondaryMeta, info.value("secondary").toObject());
        }

        applyCurrentStatus();
        const QString queriedAt = result.value("queried_at_str").toString();
        const QString prefix = queriedAt.isEmpty() ? QString("查询: 失败") : QString("查询: %1").arg(queriedAt);
        m_message->setText(prefix + "\n" + result.value("message").toString("查询失败"));
        return;
    }

    m_lastResult = result;
    const QJsonObject info = result.value("info").toObject();
    const QString plan = info.value("plan").toString();
    if (!plan.isEmpty())
        m_planBadge->setText(plan.toUpper());

    setLimit(m_primaryBar, m_primaryPercent, m_primaryMeta, info.value("primary").toObject());
    setLimit(m_secondaryBar, m_secondaryPercent, m_secondaryMeta, info.value("secondary").toObject());

    const double primary = info.value("primary").toObject().value("remaining_pct").toDouble(-1);
    const bool allowed = info.value("rate_limit_allowed").toBool(true);
    const QString state = (!allowed || (primary >= 0 && primary <= 11)) ? "error" : ((primary >= 0 && primary <= 35) ? "warning" : "ok");
    m_lastResult["status_state"] = state;
    applyCurrentStatus();
    const QString queriedAt = result.value("queried_at_str").toString();
    const QString message = result.value("message").toString();
    m_message->setText(queriedAt.isEmpty()
        ? (message.isEmpty() ? QString("查询: 已更新") : message)
        : QString("查询: %1%2").arg(queriedAt, message.isEmpty() ? QString() : QString("\n%1").arg(message)));
}

void AccountCard::setLimit(QProgressBar* bar, QLabel* percent, QLabel* meta, const QJsonObject& limit)
{
    const double remaining = limit.value("remaining_pct").toDouble(-1);
    if (remaining < 0) {
        percent->setText("剩余 --");
        bar->setValue(0);
        bar->setProperty("level", QVariant("unknown"));
        meta->setText("暂无窗口数据");
    } else {
        percent->setText(QString("剩余 %1%").arg(qRound(remaining)));
        bar->setValue(qBound(0, qRound(remaining), 100));
        bar->setProperty("level", QVariant(levelForRemaining(remaining)));
        refreshLimitTime(meta, limit);
    }
    bar->style()->unpolish(bar);
    bar->style()->polish(bar);
}

void AccountCard::refreshRelativeTimes()
{
    if (m_lastResult.value("info").toObject().isEmpty())
        return;

    const QJsonObject info = m_lastResult.value("info").toObject();
    refreshLimitTime(m_primaryMeta, info.value("primary").toObject());
    refreshLimitTime(m_secondaryMeta, info.value("secondary").toObject());
}

void AccountCard::refreshLimitTime(QLabel* meta, const QJsonObject& limit)
{
    if (!meta)
        return;

    const double remaining = limit.value("remaining_pct").toDouble(-1);
    if (remaining < 0) {
        meta->setText("暂无窗口数据");
        return;
    }

    const double resetAt = limit.value("reset_at_dt").toDouble(0);
    int seconds = limit.value("reset_after_seconds").toInt(-1);
    if (resetAt > 0) {
        const qint64 now = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
        const qint64 remainingSeconds = qMax<qint64>(0, static_cast<qint64>(resetAt) - now);
        seconds = remainingSeconds > INT_MAX ? INT_MAX : static_cast<int>(remainingSeconds);
    }
    meta->setText(QString("重置 %1\n剩余 %2").arg(formatReset(resetAt), formatSeconds(seconds)));
}

void AccountCard::applyCurrentStatus()
{
    if (m_isActive) {
        m_statusBadge->setText("使用中");
        refreshStatusStyle("active");
        return;
    }

    if (m_lastResult.isEmpty()) {
        m_statusBadge->setText("未查询");
        refreshStatusStyle("idle");
        return;
    }

    if (!m_lastResult.value("ok").toBool()) {
        m_statusBadge->setText("失败");
        refreshStatusStyle("error");
        return;
    }

    const QString state = m_lastResult.value("status_state").toString("ok");
    m_statusBadge->setText(state == "ok" ? "可用" : (state == "warning" ? "偏低" : "受限"));
    refreshStatusStyle(state);
}

QString AccountCard::levelForRemaining(double remaining)
{
    if (remaining <= 11) return "danger";
    if (remaining <= 35) return "warning";
    return "good";
}

QString AccountCard::formatSeconds(int seconds)
{
    if (seconds < 0) return "未知";
    const int days = seconds / 86400;
    seconds %= 86400;
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    QStringList parts;
    if (days) parts << QString::number(days) + "天";
    if (hours) parts << QString::number(hours) + "小时";
    if (minutes) parts << QString::number(minutes) + "分钟";
    return parts.isEmpty() ? "<1分钟" : parts.join(" ");
}

QString AccountCard::formatReset(double epochSeconds)
{
    if (epochSeconds <= 0) return "未知";
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epochSeconds)).toLocalTime().toString("MM-dd HH:mm");
}

void AccountCard::refreshStatusStyle(const QString& state)
{
    m_statusBadge->setProperty("state", QVariant(state));
    m_statusBadge->style()->unpolish(m_statusBadge);
    m_statusBadge->style()->polish(m_statusBadge);
}
