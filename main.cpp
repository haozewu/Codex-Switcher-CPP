#include "mainwindow.h"
#include "datamanager.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QNetworkProxyFactory>
#include <QNetworkProxy>
#include <QNetworkProxyQuery>
#include <QPixmap>
#include <QTcpSocket>
#include <QProcessEnvironment>
#include <QUrl>
#include <QStyleFactory>

static QNetworkProxy detectProxy()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList vars = {"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY"};
    for (const QString& var : vars) {
        QString val = env.value(var);
        if (!val.isEmpty()) {
            QUrl url(val);
            if (url.isValid() && !url.host().isEmpty()) {
                return QNetworkProxy(QNetworkProxy::HttpProxy, url.host(),
                                     url.port(url.scheme() == "https" ? 443 : 8080));
            }
        }
    }

    QNetworkProxyQuery query(QUrl("https://chatgpt.com"));
    QList<QNetworkProxy> proxies = QNetworkProxyFactory::systemProxyForQuery(query);
    if (!proxies.isEmpty() && proxies.first().type() != QNetworkProxy::NoProxy) {
        return proxies.first();
    }

    QList<int> ports = {7890, 7891, 10808, 10809, 8080, 8118};
    for (int port : ports) {
        QTcpSocket sock;
        sock.connectToHost("127.0.0.1", port);
        if (sock.waitForConnected(150)) {
            sock.disconnectFromHost();
            return QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", port);
        }
    }

    return QNetworkProxy(QNetworkProxy::NoProxy);
}

static void applyStyle(QApplication& app)
{
    app.setStyleSheet(QStringLiteral(R"(
        * {
            font-family: "Microsoft YaHei UI", "Segoe UI", "PingFang SC", sans-serif;
        }

        QWidget {
            background: transparent;
        }

        QWidget#rootWidget, QDialog {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #f7f8fa, stop:0.35 #f6f7f9, stop:1 #eef1f5);
        }

        QWidget#scrollContent, QWidget#cardContainer {
            background: transparent;
        }

        QLabel {
            background: transparent;
        }

        #appTitle {
            font-size: 28px;
            font-weight: 800;
            color: #0f172a;
        }

        #dialogTitle {
            font-size: 22px;
            font-weight: 700;
            color: #0f172a;
        }

        #mutedText {
            color: #8b9cb0;
            font-size: 14px;
        }

        #pathText {
            color: #a0aec0;
            font-size: 12px;
            font-family: Consolas, "Cascadia Mono", "JetBrains Mono", monospace;
            background: transparent;
        }

        #countBadge {
            border-radius: 20px;
            padding: 8px 18px;
            font-weight: 700;
            font-size: 14px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #dbeafe, stop:0.5 #ddd6fe, stop:1 #ede9fe);
            color: #3730a3;
        }

        #badge {
            border-radius: 12px;
            padding: 4px 12px;
            font-weight: 700;
            font-size: 12px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #f3e8ff, stop:1 #ede9fe);
            color: #6b21a8;
        }

        #statusBadge {
            border-radius: 12px;
            padding: 4px 12px;
            font-weight: 700;
            font-size: 12px;
        }
        #statusBadge[state="idle"] {
            background: #f1f5f9;
            color: #64748b;
        }
        #statusBadge[state="loading"] {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #dbeafe, stop:1 #bae6fd);
            color: #1d4ed8;
        }
        #statusBadge[state="ok"] {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #d1fae5, stop:1 #a7f3d0);
            color: #065f46;
        }
        #statusBadge[state="active"] {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #ddd6fe, stop:1 #bfdbfe);
            color: #3730a3;
        }
        #statusBadge[state="warning"] {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #fef3c7, stop:1 #fde68a);
            color: #92400e;
        }
        #statusBadge[state="error"] {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #fee2e2, stop:1 #fecaca);
            color: #991b1b;
        }

        #accountCard {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #ffffff, stop:1 #fafbfc);
            border: 1px solid #e2e8f0;
            border-radius: 8px;
        }
        #accountCard:hover {
            border: 1px solid #c0c8d4;
        }

        QFrame#cardSeparator {
            color: #e8ecf2;
        }

        #accountName {
            font-size: 18px;
            font-weight: 700;
            color: #0f172a;
        }

        #barTitle {
            color: #334155;
            font-weight: 700;
            font-size: 15px;
        }
        #barPercent {
            color: #475569;
            font-weight: 600;
            font-size: 15px;
        }

        #cardMessage {
            color: #8b9cb0;
            font-size: 15px;
            padding: 7px 11px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #f8fafc, stop:1 #f1f5f9);
            border-radius: 7px;
            border: 1px solid #e8ecf2;
        }

        QProgressBar#usageProgress {
            height: 10px;
            border: none;
            border-radius: 5px;
            background: #e4e9f0;
        }
        QProgressBar#usageProgress::chunk {
            border-radius: 5px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #10b981, stop:1 #059669);
        }
        QProgressBar#usageProgress[level="warning"]::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #f59e0b, stop:1 #d97706);
        }
        QProgressBar#usageProgress[level="danger"]::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #ef4444, stop:1 #dc2626);
        }
        QProgressBar#usageProgress[level="unknown"]::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #94a3b8, stop:1 #64748b);
        }

        QPushButton {
            border: 1px solid #d6dde8;
            border-radius: 8px;
            padding: 0px 18px;
            min-height: 40px;
            max-height: 40px;
            font-weight: 700;
            font-size: 15px;
        }
        QPushButton:disabled {
            color: #94a3b8;
            background: #e2e8f0;
        }

        #primaryButton {
            border: 1px solid #4f46e5;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4f46e5, stop:1 #4338ca);
            color: white;
        }
        #primaryButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4338ca, stop:1 #3730a3);
        }
        #primaryButton:pressed {
            background: #3730a3;
        }

        #softButton {
            border: 1px solid #8ee9c1;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #d1fae5, stop:1 #a7f3d0);
            color: #065f46;
        }
        #softButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #a7f3d0, stop:1 #6ee7b7);
        }

        #ghostButton {
            border: 1px solid #d7dee8;
            background: #f1f5f9;
            color: #475569;
        }
        #ghostButton:hover {
            background: #e2e8f0;
        }

        #dangerButton {
            border: 1px solid #f7b7b7;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #fee2e2, stop:1 #fecaca);
            color: #b91c1c;
        }
        #dangerButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #fecaca, stop:1 #fca5a5);
        }

        QScrollArea#mainScroll {
            background: transparent;
            border: none;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 11px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #cbd5e1;
            border-radius: 4px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background: #94a3b8;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }

        QPlainTextEdit#jsonEditor {
            background: #0f172a;
            color: #e2e8f0;
            border: 2px solid #334155;
            border-radius: 12px;
            padding: 14px;
            font-family: Consolas, "Cascadia Mono", "JetBrains Mono", monospace;
            font-size: 14px;
            selection-background-color: #334155;
        }
        QPlainTextEdit#jsonEditor:focus {
            border: 2px solid #6366f1;
        }

        #emptyState {
            min-height: 260px;
            color: #94a3b8;
            font-size: 16px;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #ffffff, stop:1 #fafbfc);
            border: 2px dashed #dce3ec;
            border-radius: 18px;
        }

        QMessageBox {
            background: #ffffff;
        }
    )"));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setFont(QFont("Microsoft YaHei UI", 11));
    app.setQuitOnLastWindowClosed(false);

    QIcon icon(":/app-icon.ico");
    if (icon.isNull()) {
        QPixmap px(32, 32);
        px.fill(QColor("#4f46e5"));
        icon = QIcon(px);
    }
    if (!icon.isNull()) {
        app.setWindowIcon(icon);
    }

    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QNetworkProxy proxy = detectProxy();
    if (proxy.type() != QNetworkProxy::NoProxy)
        QNetworkProxy::setApplicationProxy(proxy);

    applyStyle(app);

    MainWindow window;
    if (!icon.isNull()) {
        window.setWindowIcon(icon);
    }
    window.show();

    return app.exec();
}
