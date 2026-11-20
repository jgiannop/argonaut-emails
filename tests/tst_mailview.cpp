#include "MailWebView.h"
#include "Mime.h"
#include "TestSupport.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QWebEngineScript>

// Before the application (MAIL_TEST_MAIN makes it): WebEngine's scheme; no GPU offscreen.
static const bool webEnginePrepared = (qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu"), MailWebView::prepare(), true);

namespace {

// Something on the network to be asked: it logs every request line and answers a 1×1 PNG.
class Web : public QObject
{
public:
    QStringList requests;

    Web()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                s->setParent(this);
                connect(s, &QTcpSocket::readyRead, this, [this, s] {
                    const QByteArray head = s->readAll();
                    requests.append(QString::fromLatin1(head.left(head.indexOf('\r'))));
                    const QByteArray png = QByteArray::fromBase64(
                        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==");
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " + QByteArray::number(png.size())
                             + "\r\nConnection: close\r\n\r\n" + png);
                    s->disconnectFromHost();
                });
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

private:
    QTcpServer m_server;
};

// What a script of ours (not the page's: those never run) finds in the page.
QVariant ask(MailWebView &view, const QString &js)
{
    QVariant answer;
    bool done = false;
    view.page()->runJavaScript(js, QWebEngineScript::ApplicationWorld, [&](const QVariant &v) {
        answer = v;
        done = true;
    });
    if (!QTest::qWaitFor([&] { return done; }, 10000))
        qWarning("the page didn't answer: %s", qPrintable(js));
    return answer;
}

void showAndWait(MailWebView &view, const QString &html, const Mime::Message *message, bool remote)
{
    QSignalSpy shown(&view, &MailWebView::shown);
    view.showHtml(html, message, remote, false);
    QVERIFY(shown.wait(10000));
}

} // namespace

class TestMailView : public QObject
{
    Q_OBJECT

private slots:
    void scriptsNeverRun()
    {
        // Not through SafeHtml: the view itself must hold.
        MailWebView view;
        view.resize(600, 400);
        view.show();
        showAndWait(view,
                    QStringLiteral("<p id=\"a\">before</p><script>document.getElementById('a').textContent = 'ran';</script>"
                                   "<img src=\"data:image/gif;base64,R0lGODlhAQABAAAAACw=\" onerror=\"document.title='ran'\">"),
                    nullptr, true);
        QVERIFY(view.shownText().contains(QLatin1String("before")));
        QVERIFY(!view.shownText().contains(QLatin1String("ran")));
        QVERIFY(view.title() != QLatin1String("ran"));
    }

    void nothingFromTheNetworkUntilAsked()
    {
        Web web;
        MailWebView view;
        view.resize(600, 400);
        view.show();
        const QString html = QStringLiteral("<style>@import url('%1/import.css'); @font-face{font-family:x;src:url('%1/font.woff')}"
                                            " body{font-family:x} .b{background:url('%1/bg.png')}</style>"
                                            "<link rel=\"stylesheet\" href=\"%1/link.css\">"
                                            "<p class=\"b\">hello</p><img src=\"%1/pixel.png\">")
                                 .arg(web.base());
        showAndWait(view, html, nullptr, false);
        QTest::qWait(500);
        QVERIFY2(web.requests.isEmpty(), qPrintable(web.requests.join(u'\n')));
        QCOMPARE(ask(view, QStringLiteral("document.images[0].naturalWidth")).toInt(), 0);

        // "Show pictures": the pictures, nothing else (no style sheet, no font from there).
        showAndWait(view, html, nullptr, true);
        QTRY_VERIFY_WITH_TIMEOUT(web.requests.contains(QStringLiteral("GET /pixel.png HTTP/1.1")), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ask(view, QStringLiteral("document.images[0].naturalWidth")).toInt(), 1, 10000);
        QTest::qWait(300);
        for (const QString &r : std::as_const(web.requests))
            QVERIFY2(r.contains(QLatin1String(".png")), qPrintable(r));
    }

    void itsOwnPicturesAndLinks()
    {
        const QByteArray png = QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==");
        const Mime::Message message = Mime::parse(
            "Content-Type: multipart/related; boundary=b\r\n\r\n--b\r\nContent-Type: text/html\r\n\r\n<img src=\"cid:logo@x\">\r\n"
            "--b\r\nContent-Type: image/png\r\nContent-ID: <logo@x>\r\nContent-Transfer-Encoding: base64\r\n\r\n"
            + png.toBase64() + "\r\n--b--\r\n");
        MailWebView view;
        view.resize(600, 400);
        view.show();
        showAndWait(view,
                    QStringLiteral("<img src=\"cid:logo@x\"><a href=\"https://example.org/x\" "
                                   "style=\"display:block;height:300px\">a link</a>"),
                    &message, false);
        QTRY_COMPARE_WITH_TIMEOUT(ask(view, QStringLiteral("document.images[0].naturalWidth")).toInt(), 1, 10000);

        // A click hands the link to the window (which asks first); the page stays.
        QSignalSpy clicked(&view, &MailWebView::linkClicked);
        const QUrl before = view.url();
        QWidget *input = view.focusProxy();
        QVERIFY(input);
        QTest::mouseClick(input, Qt::LeftButton, {}, QPoint(100, 150));
        QTRY_COMPARE_WITH_TIMEOUT(clicked.size(), 1, 10000);
        QCOMPARE(clicked.first().first().toUrl(), QUrl(QStringLiteral("https://example.org/x")));
        QTest::qWait(200);
        QCOMPARE(view.url(), before);
    }
};

MAIL_TEST_MAIN(TestMailView, QApplication)
#include "tst_mailview.moc"
