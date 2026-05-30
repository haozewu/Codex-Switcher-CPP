#include "importdialog.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

ImportDialog::ImportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("导入账号");
    setMinimumSize(680, 520);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(14);

    auto* title = new QLabel("粘贴账号 JSON");
    title->setObjectName("dialogTitle");
    auto* hint = new QLabel("支持单个对象或数组，需包含 name 与 credentials。");
    hint->setObjectName("mutedText");
    m_editor = new QPlainTextEdit();
    m_editor->setObjectName("jsonEditor");
    m_editor->setPlaceholderText("[{\"name\":\"...\",\"credentials\":{\"access_token\":\"...\"}}]");

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* cancel = new QPushButton("取消");
    cancel->setObjectName("ghostButton");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* ok = new QPushButton("导入账号");
    ok->setObjectName("primaryButton");
    connect(ok, &QPushButton::clicked, this, &ImportDialog::importJson);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);

    root->addWidget(title);
    root->addWidget(hint);
    root->addWidget(m_editor, 1);
    root->addLayout(buttons);
}

QJsonArray ImportDialog::accounts() const
{
    return m_accounts;
}

void ImportDialog::importJson()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(m_editor->toPlainText().trimmed().toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, "JSON 错误", err.errorString());
        return;
    }

    QJsonArray parsed;
    if (doc.isArray()) {
        parsed = doc.array();
    } else if (doc.isObject()) {
        parsed.append(doc.object());
    }

    QJsonArray valid;
    for (const QJsonValue& value : parsed) {
        const QJsonObject obj = value.toObject();
        if (!obj.value("name").toString().isEmpty() && obj.value("credentials").isObject())
            valid.append(obj);
    }
    if (valid.isEmpty()) {
        QMessageBox::warning(this, "格式无效", "未发现有效账号。");
        return;
    }
    m_accounts = valid;
    accept();
}
