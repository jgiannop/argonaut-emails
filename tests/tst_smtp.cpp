#include "Composer.h"
#include "FakeServers.h"
#include "SmtpClient.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

class TestSmtp : public QObject
{
    Q_OBJECT

    SmtpClient::Settings settingsFor(const FakeSmtp &server)
    {
        SmtpClient::Settings s;
        s.host = QStringLiteral("127.0.0.1");
        s.port = server.port();
        s.security = SmtpClient::Security::None;
        s.user = server.user;
        s.password = server.password;
        return s;
    }

private slots:
    void sends()
    {
        FakeSmtp server;
        SmtpClient client;
        QSignalSpy sent(&client, &SmtpClient::sent);
        QSignalSpy failed(&client, &SmtpClient::failed);
        const QByteArray message = "Subject: hi\r\n\r\n.starts with a dot\r\nbye\r\n";
        client.send(settingsFor(server), QStringLiteral("maria@example.com"),
                    {QStringLiteral("john@example.org"), QStringLiteral("eleni@example.gr")}, message);
        QTRY_COMPARE(sent.size(), 1);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(server.from, QByteArray("<maria@example.com>"));
        QCOMPARE(server.to, (QList<QByteArray>{"<john@example.org>", "<eleni@example.gr>"}));
        QCOMPARE(server.data, QByteArray("Subject: hi\r\n\r\n..starts with a dot\r\nbye\r\n")); // dot-stuffed
    }

    void wrongPassword()
    {
        FakeSmtp server;
        SmtpClient client;
        QSignalSpy failed(&client, &SmtpClient::failed);
        SmtpClient::Settings s = settingsFor(server);
        s.password = QStringLiteral("wrong");
        client.send(s, QStringLiteral("maria@example.com"), {QStringLiteral("john@example.org")}, "x\r\n");
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(server.data.isEmpty());
    }

    void refusedRecipient()
    {
        FakeSmtp server;
        server.refuseRecipient = true;
        SmtpClient client;
        QSignalSpy failed(&client, &SmtpClient::failed);
        client.send(settingsFor(server), QStringLiteral("maria@example.com"), {QStringLiteral("nobody@example.org")}, "x\r\n");
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("nobody@example.org")));
        QVERIFY(server.data.isEmpty());
    }

    void neverInTheClearOverANetwork()
    {
        SmtpClient client;
        QSignalSpy failed(&client, &SmtpClient::failed);
        SmtpClient::Settings s;
        s.host = QStringLiteral("smtp.example.com");
        s.port = 25;
        s.security = SmtpClient::Security::None;
        client.send(s, QStringLiteral("maria@example.com"), {QStringLiteral("john@example.org")}, "x\r\n");
        QTRY_COMPARE(failed.size(), 1);
    }

    void noCommandsSmuggledIn()
    {
        FakeSmtp server;
        SmtpClient client;
        QSignalSpy failed(&client, &SmtpClient::failed);
        client.send(settingsFor(server), QStringLiteral("maria@example.com"), {QStringLiteral("a@b.c>\r\nRCPT TO:<evil@x.y")}, "x\r\n");
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(server.to.isEmpty());
    }
};

MAIL_TEST_MAIN(TestSmtp, QCoreApplication)
#include "tst_smtp.moc"
