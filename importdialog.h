#ifndef IMPORTDIALOG_H
#define IMPORTDIALOG_H

#include <QDialog>
#include <QJsonArray>

class QPlainTextEdit;

class ImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportDialog(QWidget* parent = nullptr);
    QJsonArray accounts() const;

private:
    void importJson();

    QPlainTextEdit* m_editor = nullptr;
    QJsonArray m_accounts;
};

#endif // IMPORTDIALOG_H
