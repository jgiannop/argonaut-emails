#include "MailStore.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QTest>

class TestStore : public QObject
{
    Q_OBJECT

private slots:
    void gmailTabs()
    {
        // A store made by 0.1, before the tabs: it gets the column, its messages are Primary.
        const QString file = QDir::homePath() + QStringLiteral("/old.sqlite");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            db.setDatabaseName(file);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE messages (account TEXT NOT NULL, folder BLOB NOT NULL, uid INTEGER NOT NULL,"
                                          " subject TEXT, from_name TEXT, from_email TEXT, to_text TEXT, date INTEGER,"
                                          " message_id BLOB, size INTEGER, seen INTEGER, flagged INTEGER, answered INTEGER,"
                                          " attachments INTEGER, body BLOB, PRIMARY KEY (account, folder, uid))")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO messages (account, folder, uid, subject, date, seen) VALUES ('a1', X'494E424F58', 1, 'old', 1, 1)")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("old"));

        MailStore store(file);
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX").size(), 1);
        QVERIFY(store.categoryCounts(QStringLiteral("a1"), "INBOX").isEmpty()); // no tabs until Gmail says
        QList<MailStore::Summary> list;
        for (int uid = 2; uid <= 4; ++uid) {
            MailStore::Summary s;
            s.account = QStringLiteral("a1");
            s.folder = "INBOX";
            s.uid = uid;
            s.date = QDateTime::currentDateTime().addDays(uid);
            list.append(s);
        }
        store.addSummaries(list);
        QSignalSpy changed(&store, &MailStore::folderChanged);
        store.setCategories(QStringLiteral("a1"), "INBOX", {{2, QStringLiteral("promotions")}, {3, QStringLiteral("promotions")}});
        QCOMPARE(changed.size(), 1);
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX", 100, QStringLiteral("primary")).size(), 2); // 1 and 4
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX", 100, QStringLiteral("promotions")).size(), 2);
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX").size(), 4);
        const auto counts = store.categoryCounts(QStringLiteral("a1"), "INBOX");
        QCOMPARE(counts.value(QStringLiteral("primary")), qMakePair(2, 1));
        QCOMPARE(counts.value(QStringLiteral("promotions")), qMakePair(2, 2));
        QCOMPARE(store.summary(QStringLiteral("a1"), "INBOX", 2).category, QStringLiteral("promotions"));
        // The same again: nothing to say, the list isn't made again.
        store.setCategories(QStringLiteral("a1"), "INBOX", {{2, QStringLiteral("promotions")}, {3, QStringLiteral("promotions")}});
        QCOMPARE(changed.size(), 1);
        // Moved to Primary in Gmail.
        store.setCategories(QStringLiteral("a1"), "INBOX", {{3, QStringLiteral("promotions")}});
        QCOMPARE(store.messages(QStringLiteral("a1"), "INBOX", 100, QStringLiteral("primary")).size(), 3);
    }

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
