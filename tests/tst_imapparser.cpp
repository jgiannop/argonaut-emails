#include "ImapParser.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QTest>

class TestImapParser : public QObject
{
    Q_OBJECT

private slots:
    void wholeResponsesWithLiterals()
    {
        QByteArray buffer = "* 1 FETCH (UID 5 BODY[] {12}\r\nHello\r\nWorl";
        QByteArray one;
        QVERIFY(!Imap::takeResponse(buffer, &one)); // the literal isn't all there
        buffer += "d)\r\nA1 OK done\r\n";
        QVERIFY(Imap::takeResponse(buffer, &one));
        QCOMPARE(one, QByteArray("* 1 FETCH (UID 5 BODY[] {12}\r\nHello\r\nWorld)"));
        const Imap::Response r = Imap::parseResponse(one);
        QVERIFY(r.isUntagged());
        QCOMPARE(r.values.size(), 3);
        QVERIFY(r.values.at(1).is("FETCH"));
        const Imap::Value *uid = Imap::fetchItem(r.values.at(2), "UID");
        QVERIFY(uid);
        QCOMPARE(uid->text, QByteArray("5"));
        const Imap::Value *body = Imap::fetchItem(r.values.at(2), "BODY[]");
        QVERIFY(body);
        QCOMPARE(body->text, QByteArray("Hello\r\nWorld"));
        QVERIFY(Imap::takeResponse(buffer, &one));
        const Imap::Response done = Imap::parseResponse(one);
        QCOMPARE(done.tag, QByteArray("A1"));
        QCOMPARE(done.status, QByteArray("OK"));
        QVERIFY(buffer.isEmpty());
    }

    void statusCodes()
    {
        const Imap::Response r = Imap::parseResponse("* OK [UIDVALIDITY 3857529045] UIDs valid");
        QCOMPARE(r.status, QByteArray("OK"));
        QCOMPARE(r.code, QByteArray("UIDVALIDITY 3857529045"));
        QCOMPARE(r.text, QStringLiteral("UIDs valid"));
        QCOMPARE(Imap::parseResponse("+ go ahead").isContinuation(), true);
    }

    void listsQuotesAndBrackets()
    {
        const QList<Imap::Value> v = Imap::parseValues("LIST (\\HasNoChildren \\Sent) \"/\" \"Sent \\\"Items\\\"\"");
        QCOMPARE(v.size(), 4);
        QVERIFY(v.at(1).isList());
        QCOMPARE(v.at(1).items.size(), 2);
        QCOMPARE(v.at(2).text, QByteArray("/"));
        QCOMPARE(v.at(3).text, QByteArray("Sent \"Items\""));
        const QList<Imap::Value> f = Imap::parseValues("(BODY[HEADER.FIELDS (FROM TO)] {3}\r\nabc FLAGS NIL)");
        QCOMPARE(f.size(), 1);
        QCOMPARE(f.at(0).items.at(0).text, QByteArray("BODY[HEADER.FIELDS (FROM TO)]"));
        QCOMPARE(f.at(0).items.at(1).text, QByteArray("abc"));
        QVERIFY(f.at(0).items.at(3).isNil());
    }

    void tooBigIsSaid()
    {
        QByteArray buffer = "* 1 FETCH (BODY[] {999999999}\r\nx";
        QByteArray one;
        bool tooBig = false;
        QVERIFY(!Imap::takeResponse(buffer, &one, &tooBig, 1000));
        QVERIFY(tooBig);
    }

    void quoting()
    {
        bool ok = false;
        QCOMPARE(Imap::quoted("a \"b\" \\c", &ok), QByteArray("\"a \\\"b\\\" \\\\c\""));
        QVERIFY(ok);
        Imap::quoted("line\r\nbreak", &ok);
        QVERIFY(!ok); // never a second command smuggled in
    }

    void mailboxNames()
    {
        QCOMPARE(Imap::decodeMailboxName("Entw&APw-rfe"), QStringLiteral("Entwürfe"));
        QCOMPARE(Imap::decodeMailboxName("Tom &- Jerry"), QStringLiteral("Tom & Jerry"));
        const QString greek = QStringLiteral("Απεσταλμένα");
        QCOMPARE(Imap::decodeMailboxName(Imap::encodeMailboxName(greek)), greek);
        QCOMPARE(Imap::encodeMailboxName(QStringLiteral("Entwürfe")), QByteArray("Entw&APw-rfe"));
    }
};

MAIL_TEST_MAIN(TestImapParser, QCoreApplication)
#include "tst_imapparser.moc"
