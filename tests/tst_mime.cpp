#include "Mime.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QTest>

class TestMime : public QObject
{
    Q_OBJECT

private slots:
    void encodedWords()
    {
        QCOMPARE(Mime::decodeWords("=?UTF-8?B?zprOsc67zrfOvM6tz4HOsQ==?="), QStringLiteral("Καλημέρα"));
        QCOMPARE(Mime::decodeWords("=?iso-8859-7?Q?=C3=E5=E9=DC_=F3=EF=F5?="), QStringLiteral("Γειά σου"));
        // The space between two encoded words isn't text; around plain words it is.
        QCOMPARE(Mime::decodeWords("=?UTF-8?Q?a?= =?UTF-8?Q?b?= c"), QStringLiteral("ab c"));
        QCOMPARE(Mime::decodeWords("Re: =?UTF-8?Q?caf=C3=A9?= time"), QStringLiteral("Re: café time"));
        // Raw UTF-8 in a header (common, if not allowed).
        QCOMPARE(Mime::decodeWords("Ζήτω"), QStringLiteral("Ζήτω"));
    }

    void quotedPrintable()
    {
        QCOMPARE(Mime::decodeQuotedPrintable("caf=C3=A9 =\r\nau lait"), QByteArray("café au lait"));
        const QByteArray text = "A line with = and trailing space \nFrom here\n.dot\n" + QByteArray(100, 'x') + "\n";
        const QByteArray encoded = Mime::encodeQuotedPrintable(text);
        for (const QByteArray &line : encoded.split('\n'))
            QVERIFY2(line.size() <= 77, line.constData());
        QByteArray back = Mime::decodeQuotedPrintable(encoded);
        back.replace("\r\n", "\n");
        QCOMPARE(back, text);
    }

    void addresses()
    {
        const QList<Mime::Address> list = Mime::parseAddresses(
            QStringLiteral("\"Papadopoulou, Maria\" <maria@example.com>, john@example.org (John), Friends: a@x.gr, b@y.gr;"));
        QCOMPARE(list.size(), 4);
        QCOMPARE(list.at(0).name, QStringLiteral("Papadopoulou, Maria"));
        QCOMPARE(list.at(0).email, QStringLiteral("maria@example.com"));
        QCOMPARE(list.at(1).name, QStringLiteral("John"));
        QCOMPARE(list.at(1).email, QStringLiteral("john@example.org"));
        QCOMPARE(list.at(3).email, QStringLiteral("b@y.gr"));
        // An encoded name with a comma in it is one person.
        const Mime::Message m = Mime::parse("From: =?UTF-8?Q?Doe=2C_John?= <john@example.org>\r\n\r\nhi\r\n");
        QCOMPARE(m.from().size(), 1);
        QCOMPARE(m.from().first().name, QStringLiteral("Doe, John"));
    }

    void dates()
    {
        const QDateTime d = Mime::parseDate(QStringLiteral("Tue, 1 Jul 2003 10:52:37 +0200 (EEST)"));
        QVERIFY(d.isValid());
        QCOMPARE(d.toUTC().time().hour(), 8);
        QVERIFY(Mime::parseDate(QStringLiteral("1 Jul 2003 10:52:37 GMT")).isValid());
    }

    void multipartWithAttachments()
    {
        const QByteArray raw = "From: Maria <maria@example.com>\r\n"
                               "To: john@example.org\r\n"
                               "Subject: =?UTF-8?B?zpHPgc+HzrXOr86x?=\r\n"
                               "Content-Type: multipart/mixed; boundary=\"outer\"\r\n"
                               "\r\n"
                               "preamble\r\n"
                               "--outer\r\n"
                               "Content-Type: multipart/alternative; boundary=inner\r\n"
                               "\r\n"
                               "--inner\r\n"
                               "Content-Type: text/plain; charset=windows-1253\r\n"
                               "Content-Transfer-Encoding: quoted-printable\r\n"
                               "\r\n"
                               "=C3=E5=E9=DC\r\n"
                               "--inner\r\n"
                               "Content-Type: text/html; charset=utf-8\r\n"
                               "\r\n"
                               "<p>Hi</p>\r\n"
                               "--inner--\r\n"
                               "--outer\r\n"
                               "Content-Type: application/pdf\r\n"
                               "Content-Disposition: attachment; filename*=UTF-8''%CE%B1%CF%81%CF%87%CE%B5%CE%AF%CE%BF.pdf\r\n"
                               "Content-Transfer-Encoding: base64\r\n"
                               "\r\n"
                               "JVBERi0x\r\nLjQK\r\n"
                               "--outer\r\n"
                               "Content-Type: text/plain; name=\"../../.bashrc\"\r\n"
                               "Content-Disposition: attachment\r\n"
                               "\r\n"
                               "evil\r\n"
                               "--outer--\r\n";
        const Mime::Message m = Mime::parse(raw);
        QCOMPARE(m.subject(), QStringLiteral("Αρχεία"));
        QCOMPARE(m.plainText().trimmed(), QStringLiteral("Γειά"));
        QCOMPARE(m.html().trimmed(), QStringLiteral("<p>Hi</p>"));
        const QList<Mime::Attachment> files = m.attachments();
        QCOMPARE(files.size(), 2);
        QCOMPARE(files.at(0).fileName, QStringLiteral("αρχείο.pdf"));
        QCOMPARE(files.at(0).part->body, QByteArray("%PDF-1.4\n"));
        QCOMPARE(files.at(1).fileName, QStringLiteral(".bashrc")); // never a path
    }

    void forwardedMessageInside()
    {
        const QByteArray raw = "Content-Type: multipart/mixed; boundary=b\r\n\r\n"
                               "--b\r\nContent-Type: text/plain\r\n\r\nSee below\r\n"
                               "--b\r\nContent-Type: message/rfc822\r\n\r\nSubject: Inner\r\n\r\ninner text\r\n"
                               "--b--\r\n";
        const Mime::Message m = Mime::parse(raw);
        QCOMPARE(m.plainText().trimmed(), QStringLiteral("See below"));
        const QList<Mime::Attachment> files = m.attachments();
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().type, QByteArray("message/rfc822"));
        QCOMPARE(files.first().part->children.first().header("Subject"), QStringLiteral("Inner"));
    }

    void brokenMessagesStillRead()
    {
        QVERIFY(Mime::parse(QByteArray()).plainText().isEmpty());
        const Mime::Message noBoundary = Mime::parse("Content-Type: multipart/mixed\r\n\r\ntext");
        QVERIFY(noBoundary.attachments().isEmpty());
        // Nested beyond reason: stops, doesn't crash.
        QByteArray deep;
        for (int i = 0; i < 200; ++i)
            deep += "Content-Type: multipart/mixed; boundary=b" + QByteArray::number(i) + "\r\n\r\n--b" + QByteArray::number(i) + "\r\n";
        Mime::parse(deep);
    }
};

MAIL_TEST_MAIN(TestMime, QCoreApplication)
#include "tst_mime.moc"
