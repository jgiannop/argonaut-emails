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
            "<html><head><title>T</title><meta http-equiv=\"refresh\" content=\"0;url=https://x\">"
            "<style>p{color:red} .hero{background:url('https://tracker.example/bg2.png')}</style></head><body onload=\"x()\">"
            "<!-- hidden --><script>alert(1)</script><p style=\"color:blue\" onclick=\"y()\">Hello</p>"
            "<img src=\"https://tracker.example/pixel.gif\" alt=\"logo\" width=\"10\"><img src=\"cid:part1\">"
            "<img src=\"data:image/png;base64,AAAA\"><img src=\"file:///etc/passwd\"><img src=\"javascript:x()\">"
            "<a href=\"javascript:evil()\">bad</a><a href=\"https://example.org\" target=\"_blank\" ping=\"https://p\">good</a>"
            "<iframe src=\"https://x\">frame text</iframe><form action=\"https://x\"><input value=\"v\">kept</form>"
            "<div style=\"background:url(https://tracker.example/bg.png)\">bg</div><script>never closed"));
        QVERIFY(!r.html.contains(QLatin1String("alert")));
        QVERIFY(!r.html.contains(QLatin1String("onload")));
        QVERIFY(!r.html.contains(QLatin1String("onclick")));
        QVERIFY(!r.html.contains(QLatin1String("hidden")));
        QVERIFY(!r.html.contains(QLatin1String("refresh")));
        QVERIFY(!r.html.contains(QLatin1String("<title")));
        QVERIFY(!r.html.contains(QLatin1String("javascript")));
        QVERIFY(!r.html.contains(QLatin1String("file:")));
        QVERIFY(!r.html.contains(QLatin1String("frame text")));
        QVERIFY(!r.html.contains(QLatin1String("never closed")));
        QVERIFY(!r.html.contains(QLatin1String("target=")));
        QVERIFY(!r.html.contains(QLatin1String("ping=")));
        QVERIFY(!r.html.contains(QLatin1String("<form")));
        QVERIFY(r.html.contains(QLatin1String("Hello")));
        // The look stays: the style sheet, style attributes.
        QVERIFY(r.html.contains(QLatin1String("<style>p{color:red}")));
        QVERIFY(r.html.contains(QLatin1String("color:blue")));
        // Pictures stay where they are; the view decides what loads.
        QVERIFY(r.html.contains(QLatin1String("<img src=\"https://tracker.example/pixel.gif\" alt=\"logo\" width=\"10\">")));
        QVERIFY(r.html.contains(QLatin1String("cid:part1")));
        QVERIFY(r.html.contains(QLatin1String("data:image/png;base64,AAAA")));
        QVERIFY(r.html.contains(QLatin1String("https://example.org")));
        QVERIFY(r.html.contains(QLatin1String("kept")));
        QCOMPARE(r.remotePictures, 3); // the <img>, the style sheet's url(), the style attribute's url()
    }

    void styleCantHideATag()
    {
        // The style sheet ends where the browser ends it; what follows is cleaned as usual.
        const SafeHtml::Result r = SafeHtml::clean(QStringLiteral("<style>a{}</STYLE ><img src=x onerror=alert(1)><script>bad()</script>"));
        QVERIFY(r.html.startsWith(QLatin1String("<style>a{}</style>")));
        QVERIFY(!r.html.contains(QLatin1String("onerror")));
        QVERIFY(!r.html.contains(QLatin1String("bad()")));
        QVERIFY(!r.html.contains(QLatin1String("<img"))); // "x" is no picture
        // Never closed: what is left is CSS, never tags.
        const SafeHtml::Result open = SafeHtml::clean(QStringLiteral("<style>p{}<script>bad()</script>"));
        QCOMPARE(open.html, QStringLiteral("<style>p{}<script>bad()</script></style>"));
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
