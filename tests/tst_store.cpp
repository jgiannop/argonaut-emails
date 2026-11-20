#include "MailStore.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTest>

class TestStore : public QObject
{
    Q_OBJECT

private slots:
    void keepsAndForgets()
    {
        MailStore store;
        QVERIFY(store.isOpen());
        QCOMPARE(QFileInfo(MailStore::defaultFile()).permissions() & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());
        MailStore::Folder inbox;
        inbox.path = "INBOX";
        inbox.name = QStringLiteral("Inbox");
        inbox.role = QStringLiteral("inbox");
        store.setFolders(QStringLiteral("a1"), {inbox});
        store.setUidValidity(QStringLiteral("a1"), "INBOX", 5);
        QList<MailStore::Summary> list;
        for (int uid = 1; uid <= 3; ++uid) {
            MailStore::Summary s;
            s.account = QStringLiteral("a1");
            s.folder = "INBOX";
            s.uid = uid;
            s.subject = QStringLiteral("Message %1").arg(uid);
            s.date = QDateTime::currentDateTime().addDays(uid);
            s.seen = uid == 1;
            list.append(s);
        }
        store.addSummaries(list);
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX").size(), 3);
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX").first().uid, 3); // newest first
        QCOMPARE(store.folder(QStringLiteral("a1"), "INBOX").unread, 2);
        QCOMPARE(store.highestUid(QStringLiteral("a1"), "INBOX"), 3);
        store.setBody(QStringLiteral("a1"), "INBOX", 2, "raw");
        QCOMPARE(store.body(QStringLiteral("a1"), "INBOX", 2), QByteArray("raw"));
        store.removeMissing(QStringLiteral("a1"), "INBOX", 1, 3, {1, 3});
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX").size(), 2);
        // A new UIDVALIDITY: what was kept means nothing now.
        store.setUidValidity(QStringLiteral("a1"), "INBOX", 6);
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX").size(), 0);
        // A folder that went away takes its messages along.
        store.addSummaries(list);
        store.setFolders(QStringLiteral("a1"), {});
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX").size(), 0);
    }
};

MAIL_TEST_MAIN(TestStore, QCoreApplication)
#include "tst_store.moc"
