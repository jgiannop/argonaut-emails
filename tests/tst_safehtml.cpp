#include "SafeHtml.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QTest>

class TestSafeHtml : public QObject
{
    Q_OBJECT

private slots:
    void whatCantBeUsedIsTakenOut()
    {
        const SafeHtml::Result r = SafeHtml::clean(QStringLiteral(
            "<html><head><title>T</title><style>p{color:red}</style></head><body onload=\"x()\">"
            "<!-- hidden --><script>alert(1)</script><p style=\"color:blue\" onclick=\"y()\">Hello</p>"
            "<img src=\"https://tracker.example/pixel.gif\" alt=\"logo\"><img src=\"cid:part1\">"
            "<a href=\"javascript:evil()\">bad</a><a href=\"https://example.org\">good</a>"
            "<iframe src=\"https://x\">frame text</iframe><form action=\"https://x\"><input value=\"v\">kept</form>"
            "<div style=\"background:url(https://tracker.example/bg.png)\">bg</div><script>never closed"));
        QVERIFY(!r.html.contains(QLatin1String("alert")));
        QVERIFY(!r.html.contains(QLatin1String("onload")));
        QVERIFY(!r.html.contains(QLatin1String("onclick")));
        QVERIFY(!r.html.contains(QLatin1String("hidden")));
        QVERIFY(!r.html.contains(QLatin1String("color:red")));
        QVERIFY(!r.html.contains(QLatin1String("tracker.example")));
        QVERIFY(!r.html.contains(QLatin1String("javascript")));
        QVERIFY(!r.html.contains(QLatin1String("frame text")));
        QVERIFY(!r.html.contains(QLatin1String("never closed")));
        QVERIFY(r.html.contains(QLatin1String("Hello")));
        QVERIFY(r.html.contains(QLatin1String("color:blue")));
        QVERIFY(r.html.contains(QLatin1String("[logo]")));
        QVERIFY(r.html.contains(QLatin1String("cid:part1")));
        QVERIFY(r.html.contains(QLatin1String("https://example.org")));
        QVERIFY(r.html.contains(QLatin1String("kept")));
        QCOMPARE(r.remotePictures, 2);
    }

    void plainText()
    {
        const QString html = SafeHtml::fromPlainText(QStringLiteral("<b>not bold</b> see https://example.org/a?b=1&c=2\n> quoted"));
        QVERIFY(html.contains(QLatin1String("&lt;b&gt;")));
        QVERIFY(html.contains(QLatin1String("<a href=\"https://example.org/a?b=1&amp;c=2\">")));
        QVERIFY(html.contains(QLatin1String("#777777")));
    }
};

MAIL_TEST_MAIN(TestSafeHtml, QCoreApplication)
#include "tst_safehtml.moc"
