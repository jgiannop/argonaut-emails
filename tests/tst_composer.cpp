#include "Composer.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QTest>

class TestComposer : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip()
    {
        Composer::Draft d;
        d.from = {QStringLiteral("Μαρία Παπαδοπούλου"), QStringLiteral("maria@example.com")};
        d.to = {{QStringLiteral("Doe, John"), QStringLiteral("john@example.org")}};
        d.cc = {{QString(), QStringLiteral("eleni@example.gr")}};
        d.bcc = {{QString(), QStringLiteral("secret@example.gr")}};
        d.subject = QStringLiteral("Συνάντηση αύριο στις 10, στο γραφείο της εταιρείας στην Αθήνα");
        d.text = QStringLiteral("Γεια σου Γιάννη,\n.a line with a dot\nFrom the start\n");
        d.files = {{QStringLiteral("σημειώσεις.txt"), QByteArray(), QByteArray("notes\n")}};
        const QByteArray raw = Composer::build(d);
        QVERIFY(!raw.contains("secret@example.gr")); // Bcc is only in the envelope
        for (const QByteArray &line : raw.split('\n'))
            QVERIFY2(line.size() <= 78, line.constData());
        const Mime::Message m = Mime::parse(raw);
        QCOMPARE(m.subject(), d.subject);
        QCOMPARE(m.from().first().name, d.from.name);
        QCOMPARE(m.to().first().name, QStringLiteral("Doe, John"));
        QCOMPARE(m.cc().first().email, QStringLiteral("eleni@example.gr"));
        QCOMPARE(m.plainText().replace(QLatin1String("\r\n"), QLatin1String("\n")), d.text);
        QCOMPARE(m.attachments().size(), 1);
        QCOMPARE(m.attachments().first().fileName, QStringLiteral("σημειώσεις.txt"));
        QCOMPARE(m.attachments().first().part->body, QByteArray("notes\n"));
        QVERIFY(!m.messageId().isEmpty());
        QCOMPARE(Composer::recipients(d),
                 (QStringList{QStringLiteral("john@example.org"), QStringLiteral("eleni@example.gr"), QStringLiteral("secret@example.gr")}));
    }

    void noHeadersSmuggledIn()
    {
        Composer::Draft d;
        d.from = {QStringLiteral("Evil\r\nBcc: everyone@example.org"), QStringLiteral("me@example.com")};
        d.to = {{QString(), QStringLiteral("you@example.org")}, {QString(), QStringLiteral("x@y.z>\r\nBcc: z@z.z")}};
        d.subject = QStringLiteral("Hi\r\nBcc: all@example.org");
        const QByteArray raw = Composer::build(d);
        QVERIFY(!raw.contains("\r\nBcc:"));
        QVERIFY(!raw.contains("\nBcc:"));
        QCOMPARE(Composer::recipients(d), QStringList{QStringLiteral("you@example.org")});
    }

    void replies()
    {
        QCOMPARE(Composer::replySubject(QStringLiteral("Hello")), QStringLiteral("Re: Hello"));
        QCOMPARE(Composer::replySubject(QStringLiteral("RE: Hello")), QStringLiteral("RE: Hello"));
        QCOMPARE(Composer::forwardSubject(QStringLiteral("Hello")), QStringLiteral("Fwd: Hello"));
        const Mime::Message m = Mime::parse("From: Maria <maria@example.com>\r\nDate: Tue, 1 Jul 2003 10:52:37 +0200\r\n\r\nline one\r\n> older\r\n");
        const QString q = Composer::quoted(m);
        QVERIFY(q.contains(QStringLiteral("Maria wrote:")));
        QVERIFY(q.contains(QStringLiteral("> line one")));
        QVERIFY(q.contains(QStringLiteral(">> older")));
        Composer::Draft d;
        d.from = {QString(), QStringLiteral("me@example.com")};
        d.to = {{QString(), QStringLiteral("maria@example.com")}};
        d.inReplyTo = "abc@example.com";
        d.references = "<first@example.com>";
        const Mime::Message reply = Mime::parse(Composer::build(d));
        QCOMPARE(reply.inReplyTo(), QByteArray("abc@example.com"));
        QVERIFY(reply.references().contains("<first@example.com>"));
        QVERIFY(reply.references().contains("<abc@example.com>"));
    }
};

MAIL_TEST_MAIN(TestComposer, QCoreApplication)
#include "tst_composer.moc"
