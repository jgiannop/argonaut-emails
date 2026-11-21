#include "FakeServers.h"
#include "ImapClient.h"
#include "MailStore.h"
#include "Sync.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

namespace {

QByteArray mail(int n, const QByteArray &extra = {})
{
    return "From: =?UTF-8?Q?M=C3=A1ria?= <maria@example.com>\r\n"
           "To: John <john@example.org>\r\n"
           "Subject: Message " + QByteArray::number(n) + "\r\n"
           "Date: Tue, 1 Jul 2025 10:0" + QByteArray::number(n % 10) + ":00 +0200\r\n"
           "Message-ID: <m" + QByteArray::number(n) + "@example.com>\r\n" + extra +
           "\r\n"
           "Body of message " + QByteArray::number(n) + "\r\n";
}

} // namespace

class TestSync : public QObject
{
    Q_OBJECT

    Account account()
    {
        Account a;
        a.id = QStringLiteral("11111111-2222-3333-4444-555555555555");
        a.email = QStringLiteral("maria@example.com");
        a.imapHost = QStringLiteral("127.0.0.1");
        return a;
    }
    ImapClient::Settings settingsFor(const FakeImap &server)
    {
        ImapClient::Settings s;
        s.host = QStringLiteral("127.0.0.1");
        s.port = server.port();
        s.security = ImapClient::Security::None;
        s.user = server.user;
        s.password = server.password;
        return s;
    }
    void waitIdle(Sync &sync)
    {
        QTRY_VERIFY_WITH_TIMEOUT(!sync.isBusy(), 10000);
    }

private slots:
    void clientLogsIn()
    {
        FakeImap server;
        ImapClient client;
        QSignalSpy ready(&client, &ImapClient::ready);
        client.open(settingsFor(server));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(server.loggedCommand("AUTHENTICATE PLAIN "));
        QVERIFY(client.hasCapability("MOVE"));
        // Without SASL-IR or AUTH=PLAIN: LOGIN with quoted words.
        FakeImap old;
        old.capabilities = "IMAP4rev1";
        ImapClient second;
        QSignalSpy secondReady(&second, &ImapClient::ready);
        second.open(settingsFor(old));
        QTRY_COMPARE(secondReady.size(), 1);
        QVERIFY(old.loggedCommand("LOGIN \"maria@example.com\""));

        // Like Gmail: MOVE is said only after logging in, and not in the login's answer.
        FakeImap gmail;
        gmail.beforeLogin = "IMAP4rev1 AUTH=PLAIN SASL-IR X-GM-EXT-1";
        gmail.capabilities = "IMAP4rev1 X-GM-EXT-1 MOVE UIDPLUS";
        gmail.capabilityInLoginAnswer = false;
        ImapClient third;
        QSignalSpy thirdReady(&third, &ImapClient::ready);
        third.open(settingsFor(gmail));
        QTRY_COMPARE(thirdReady.size(), 1);
        QVERIFY(third.hasCapability("MOVE"));
    }

    void wrongPasswordIsSaid()
    {
        FakeImap server;
        ImapClient client;
        QSignalSpy failed(&client, &ImapClient::failed);
        ImapClient::Settings s = settingsFor(server);
        s.password = QStringLiteral("wrong");
        client.open(s);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("Invalid credentials")));
    }

    void neverInTheClearOverANetwork()
    {
        ImapClient client;
        QSignalSpy failed(&client, &ImapClient::failed);
        ImapClient::Settings s;
        s.host = QStringLiteral("imap.example.com");
        s.port = 143;
        s.security = ImapClient::Security::None;
        s.user = QStringLiteral("u");
        s.password = QStringLiteral("p");
        client.open(s);
        QTRY_COMPARE(failed.size(), 1);
    }

    void foldersAndMessages()
    {
        FakeImap server;
        for (int i = 1; i <= 3; ++i)
            server.add("INBOX", mail(i), i == 1 ? QStringList{QStringLiteral("\\Seen")} : QStringList());
        MailStore store(QDir::homePath() + QStringLiteral("/first.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.start();
        waitIdle(sync);
        const QList<MailStore::Folder> folders = store.folders(account().id);
        QCOMPARE(folders.size(), 3);
        QCOMPARE(folders.first().path, QByteArray("INBOX"));
        QCOMPARE(store.folder(account().id, "Sent").role, QStringLiteral("sent"));
        QList<MailStore::Summary> list = store.messages(account().id, "INBOX");
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.first().subject, QStringLiteral("Message 3"));
        QCOMPARE(list.first().fromName, QStringLiteral("Mária"));
        QCOMPARE(store.folder(account().id, "INBOX").unread, 2);

        // New mail: only it is fetched; and one read elsewhere.
        server.add("INBOX", mail(4));
        server.find("INBOX", 2)->flags.append(QStringLiteral("\\Seen"));
        sync.syncFolder("INBOX");
        waitIdle(sync);
        list = store.messages(account().id, "INBOX");
        QCOMPARE(list.size(), 4);
        QVERIFY(store.summary(account().id, "INBOX", 2).seen);
        QVERIFY(server.loggedCommand("UID FETCH 4:*"));

        // Opening one: fetched once, then kept.
        QByteArray raw;
        sync.fetchBody("INBOX", 3, [&raw](const QByteArray &r, const QString &) { raw = r; });
        waitIdle(sync);
        QVERIFY(raw.contains("Body of message 3"));
        server.log.clear();
        sync.fetchBody("INBOX", 3, [&raw](const QByteArray &r, const QString &) { raw = r; });
        QVERIFY(server.log.isEmpty());

        // Read, starred, then to the Trash.
        sync.setSeen("INBOX", 3, true);
        sync.setFlagged("INBOX", 3, true);
        waitIdle(sync);
        QVERIFY(server.find("INBOX", 3)->flags.contains(QStringLiteral("\\Seen")));
        QVERIFY(server.find("INBOX", 3)->flags.contains(QStringLiteral("\\Flagged")));
        sync.moveToTrash("INBOX", 3);
        waitIdle(sync);
        QVERIFY(!server.find("INBOX", 3));
        QCOMPARE(server.boxes["Trash"].mails.size(), 1);
        QCOMPARE(store.messages(account().id, "Trash").size(), 1);

        // Deleted elsewhere: gone here at the next look.
        server.boxes["INBOX"].mails.removeFirst();
        sync.syncFolder("INBOX");
        waitIdle(sync);
        QCOMPARE(store.messages(account().id, "INBOX").size(), 2);

        // A copy of a sent message.
        sync.keepSent("Subject: sent\r\n\r\nhello\r\n");
        waitIdle(sync);
        QCOMPARE(server.boxes["Sent"].mails.size(), 1);
        QCOMPARE(server.boxes["Sent"].mails.first().raw, QByteArray("Subject: sent\r\n\r\nhello\r\n"));
    }

    void specialFoldersByFlagsFirst()
    {
        // Gmail with labels from an old Outlook: only the flags say which is the Trash.
        FakeImap server;
        server.boxes.clear();
        server.boxes["INBOX"];
        server.boxes["Deleted Items"];
        server.boxes["Sent Items"];
        server.boxes["[Gmail]"].role = "\\Noselect";
        server.boxes["[Gmail]/All Mail"].role = "\\All";
        server.boxes["[Gmail]/Sent Mail"].role = "\\Sent";
        server.boxes["[Gmail]/Starred"].role = "\\Flagged";
        server.boxes["[Gmail]/Trash"].role = "\\Trash";
        server.add("INBOX", mail(1));
        MailStore store(QDir::homePath() + QStringLiteral("/gmail.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.start();
        waitIdle(sync);
        QCOMPARE(store.folder(account().id, "Deleted Items").role, QString());
        QCOMPARE(store.folder(account().id, "Sent Items").role, QString());
        QCOMPARE(store.folder(account().id, "[Gmail]/Trash").role, QStringLiteral("trash"));
        QCOMPARE(store.folder(account().id, "[Gmail]/Sent Mail").role, QStringLiteral("sent"));
        QCOMPARE(store.folder(account().id, "[Gmail]/Starred").role, QStringLiteral("starred"));
        const MailStore::Folder all = store.folder(account().id, "[Gmail]/All Mail");
        QCOMPARE(all.role, QStringLiteral("all"));
        QCOMPARE(all.name, QStringLiteral("All Mail"));
        const QList<MailStore::Folder> folders = store.folders(account().id);
        QCOMPARE(folders.at(0).path, QByteArray("INBOX"));
        QList<QByteArray> order;
        for (const MailStore::Folder &f : folders)
            order.append(f.path);
        // The special ones first, from inside "[Gmail]" too; then the labels by name.
        QCOMPARE(order, (QList<QByteArray>{"INBOX", "[Gmail]/All Mail", "[Gmail]/Starred", "[Gmail]/Sent Mail", "[Gmail]/Trash",
                                           "[Gmail]", "Deleted Items", "Sent Items"}));
        QVERIFY(!store.folder(account().id, "[Gmail]").selectable);

        sync.moveToTrash("INBOX", 1);
        waitIdle(sync);
        QCOMPARE(server.boxes["[Gmail]/Trash"].mails.size(), 1);
        QVERIFY(server.boxes["Deleted Items"].mails.isEmpty());

        // A server that marks nothing: the names are all there is.
        FakeImap plain;
        plain.boxes.clear();
        plain.boxes["INBOX"];
        plain.boxes["Deleted Items"];
        plain.boxes["Sent Items"];
        MailStore other(QDir::homePath() + QStringLiteral("/plain.sqlite"));
        Sync plainSync(account(), &other);
        plainSync.setSettings(settingsFor(plain));
        plainSync.start();
        waitIdle(plainSync);
        QCOMPARE(other.folder(account().id, "Deleted Items").role, QStringLiteral("trash"));
        QCOMPARE(other.folder(account().id, "Sent Items").role, QStringLiteral("sent"));
    }

    void gmailInboxTabs()
    {
        FakeImap server;
        server.capabilities += " X-GM-EXT-1";
        server.add("INBOX", mail(1));
        server.add("INBOX", mail(2));
        server.add("INBOX", mail(3));
        server.add("INBOX", mail(4));
        server.boxes["INBOX"].mails[1].category = "promotions";
        server.boxes["INBOX"].mails[2].category = "updates";
        server.boxes["INBOX"].mails[3].category = "updates,primary"; // Gmail shows it in both
        MailStore store(QDir::homePath() + QStringLiteral("/tabs.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.start();
        waitIdle(sync);
        QVERIFY(server.loggedCommand("UID SEARCH X-GM-RAW \"category:primary\""));
        QVERIFY(server.loggedCommand("UID SEARCH X-GM-RAW \"category:promotions\""));
        QVERIFY(server.loggedCommand("UID SEARCH X-GM-RAW \"category:forums\""));
        QCOMPARE(store.messages(account().id, "INBOX", 100, QStringLiteral("primary")).size(), 2); // 1 (no tab) and 4 (both)
        QCOMPARE(store.messages(account().id, "INBOX", 100, QStringLiteral("primary")).first().uid, 4);
        QCOMPARE(store.messages(account().id, "INBOX", 100, QStringLiteral("promotions")).first().uid, 2);
        QCOMPARE(store.messages(account().id, "INBOX", 100, QStringLiteral("updates")).size(), 2);

        // New mail in Social, and one moved to Primary in Gmail: both seen at the next look.
        server.add("INBOX", mail(5));
        server.boxes["INBOX"].mails[4].category = "social";
        server.boxes["INBOX"].mails[3].category.clear();
        sync.syncFolder("INBOX");
        waitIdle(sync);
        QCOMPARE(store.summary(account().id, "INBOX", 5).categories, QStringList{QStringLiteral("social")});
        QCOMPARE(store.messages(account().id, "INBOX", 100, QStringLiteral("primary")).size(), 2); // 1 and 4
        QCOMPARE(store.messages(account().id, "INBOX", 100, QStringLiteral("updates")).size(), 1); // 3

        // Only the inbox has tabs.
        server.log.clear();
        sync.syncFolder("Sent");
        waitIdle(sync);
        QVERIFY(!server.loggedCommand("UID SEARCH"));
    }

    void noTabsWithoutGmail()
    {
        FakeImap server;
        server.add("INBOX", mail(1));
        MailStore store(QDir::homePath() + QStringLiteral("/notabs.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.start();
        waitIdle(sync);
        QVERIFY(!server.loggedCommand("UID SEARCH"));
        QVERIFY(store.categoryCounts(account().id, "INBOX").isEmpty());
    }

    // A big one (an attachment): its text isn't fetched ahead, only when opened.
    static QByteArray bigMail(int n)
    {
        QByteArray padding;
        for (int i = 0; i < 9000; ++i)
            padding += QByteArray(70, 'x') + "\r\n";
        return mail(n) + padding;
    }

    void openingAMessageDoesntWait()
    {
        FakeImap server;
        server.add("INBOX", bigMail(1));
        for (const QByteArray box : {"A", "B", "C", "D"})
            server.add(box, mail(2));
        MailStore store(QDir::homePath() + QStringLiteral("/urgent.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.start();
        waitIdle(sync);
        QVERIFY(store.body(account().id, "INBOX", 1).isEmpty()); // too big to be fetched ahead
        server.log.clear();
        // Folders to check, then a message opened: on a connection of its own, it doesn't wait for them.
        for (const QByteArray box : {"A", "B", "C", "D"})
            sync.syncFolder(box);
        QByteArray raw;
        sync.fetchBody("INBOX", 1, [&raw](const QByteArray &r, const QString &) { raw = r; });
        waitIdle(sync);
        QVERIFY(raw.contains("Body of message 1"));
        const qsizetype body = server.log.indexOf("UID FETCH 1 (UID BODY.PEEK[])");
        QVERIFY2(body >= 0, qPrintable(QString::fromLatin1(server.log.join('\n'))));
        QVERIFY(server.loggedCommand("EXAMINE")); // that connection only reads
        qsizetype lastSelect = -1;
        for (qsizetype i = 0; i < server.log.size(); ++i) {
            if (server.log.at(i).startsWith("SELECT"))
                lastSelect = i;
        }
        QVERIFY(body < lastSelect); // folders B, C, D were checked after it
    }

    void newestMessagesAreFetchedAhead()
    {
        FakeImap server;
        for (int i = 1; i <= 3; ++i)
            server.add("INBOX", mail(i));
        server.add("INBOX", bigMail(4));
        server.add("Trash", mail(5));
        MailStore store(QDir::homePath() + QStringLiteral("/ahead.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.start();
        sync.syncFolder("Trash");
        waitIdle(sync);
        for (qint64 uid : {1, 2, 3})
            QVERIFY(store.body(account().id, "INBOX", uid).contains("Body of message"));
        QVERIFY(store.body(account().id, "INBOX", 4).isEmpty()); // big: when opened
        QVERIFY(store.body(account().id, "Trash", 1).isEmpty()); // not the Trash
        // Opened: from the store, nothing asked.
        server.log.clear();
        QByteArray raw;
        sync.fetchBody("INBOX", 2, [&raw](const QByteArray &r, const QString &) { raw = r; });
        QVERIFY(raw.contains("Body of message 2"));
        QVERIFY(server.log.isEmpty());
    }

    // After a sleep or a change of network the connections are dead but don't
    // know it: a quiet one is asked first, and replaced in moments.
    void aDeadLineIsReplaced()
    {
        FakeImap server;
        server.add("INBOX", bigMail(1));
        server.add("INBOX", bigMail(2));
        MailStore store(QDir::homePath() + QStringLiteral("/dead.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.setTimeouts(300, 500, 20000);
        sync.start();
        waitIdle(sync);
        QByteArray raw;
        sync.fetchBody("INBOX", 1, [&raw](const QByteArray &r, const QString &) { raw = r; }); // the message connection opens
        waitIdle(sync);
        QVERIFY(raw.contains("Body of message 1"));
        const int before = server.connections();

        server.goDeaf();
        server.add("INBOX", mail(3));
        QTest::qWait(400); // quiet
        QSignalSpy errors(&sync, &Sync::error);
        QElapsedTimer timer;
        timer.start();
        QString error;
        raw.clear();
        sync.fetchBody("INBOX", 2, [&](const QByteArray &r, const QString &e) {
            raw = r;
            error = e;
        });
        sync.syncFolder("INBOX");
        QTRY_VERIFY_WITH_TIMEOUT(!raw.isEmpty() && store.messages(account().id, "INBOX").size() == 3, 10000);
        QVERIFY2(raw.contains("Body of message 2"), qPrintable(error));
        QVERIFY(timer.elapsed() < 5000); // not the minute a command waits
        QCOMPARE(errors.size(), 0);       // nothing for the user to see
        QCOMPARE(server.connections(), before + 2);
        waitIdle(sync);
    }

    // A line that dies while a message is being fetched: the message comes
    // over a new connection, without a second click.
    void aMessageIsTriedAgainOnANewLine()
    {
        FakeImap server;
        server.add("INBOX", bigMail(1));
        server.add("INBOX", bigMail(2));
        MailStore store(QDir::homePath() + QStringLiteral("/again.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.setTimeouts(60000, 500, 800);
        sync.start();
        waitIdle(sync);
        QByteArray raw;
        sync.fetchBody("INBOX", 1, [&raw](const QByteArray &r, const QString &) { raw = r; });
        waitIdle(sync);
        server.goDeaf();
        raw.clear();
        QString error = QStringLiteral("not yet");
        sync.fetchBody("INBOX", 2, [&](const QByteArray &r, const QString &e) {
            raw = r;
            error = e;
        });
        QTRY_VERIFY_WITH_TIMEOUT(error != QStringLiteral("not yet"), 10000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(raw.contains("Body of message 2"));
    }

    void newUidValidityStartsAgain()
    {
        FakeImap server;
        server.add("INBOX", mail(1));
        MailStore store(QDir::homePath() + QStringLiteral("/second.sqlite"));
        Sync sync(account(), &store);
        sync.setSettings(settingsFor(server));
        sync.start();
        waitIdle(sync);
        QCOMPARE(store.messages(account().id, "INBOX").size(), 1);
        server.boxes["INBOX"].uidValidity = 99;
        server.boxes["INBOX"].mails.first().raw = mail(9);
        sync.syncFolder("INBOX");
        waitIdle(sync);
        const QList<MailStore::Summary> list = store.messages(account().id, "INBOX");
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().subject, QStringLiteral("Message 9"));
    }
};

MAIL_TEST_MAIN(TestSync, QCoreApplication)
#include "tst_sync.moc"
