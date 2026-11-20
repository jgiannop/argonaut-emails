#include "Account.h"
#include "ComposeWindow.h"
#include "FakeServers.h"
#include "FolderTree.h"
#include "MainWindow.h"
#include "MessageList.h"
#include "MessageView.h"
#include "Passwords.h"
#include "Sync.h"
#include "TestSupport.h"

#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void readReplyAndSend()
    {
        FakeImap imap;
        FakeSmtp smtp;
        imap.add("INBOX", "From: John <john@example.org>\r\nTo: maria@example.com\r\nSubject: Lunch?\r\n"
                          "Message-ID: <lunch@example.org>\r\nContent-Type: text/html\r\n\r\n"
                          "<p>Shall we eat at <b>one</b>?</p><img src=\"https://tracker.example/p.gif\"><script>bad()</script>\r\n");
        Account a;
        a.email = QStringLiteral("maria@example.com");
        a.name = QStringLiteral("Maria");
        a.imapHost = QStringLiteral("127.0.0.1");
        a.imapPort = imap.port();
        a.imapSecurity = ImapClient::Security::None;
        a.smtpHost = QStringLiteral("127.0.0.1");
        a.smtpPort = smtp.port();
        a.smtpSecurity = ImapClient::Security::None;
        Account::save(a);
        QVERIFY(Passwords::save(a.id, QStringLiteral("imap"), imap.password, a.email));

        MailStore store;
        MainWindow window(&store);
        window.show();
        Sync *sync = window.syncFor(a.id);
        QVERIFY(sync);
        connect(sync, &Sync::error, this, [](const QString &e) { qWarning("sync: %s", qPrintable(e)); });
        QTRY_VERIFY_WITH_TIMEOUT(!sync->isBusy() && window.list()->messages()->rowCount() == 1, 10000);
        QCOMPARE(window.folders()->currentFolder(), QByteArray("INBOX"));

        window.list()->setCurrentIndex(window.list()->model()->index(0, 0));
        QTRY_VERIFY(window.view()->hasMessage());
        QVERIFY(window.view()->shownText().contains(QStringLiteral("Shall we eat at one?")));
        QVERIFY(!window.view()->shownText().contains(QStringLiteral("bad()")));
        QTRY_VERIFY(imap.find("INBOX", 1)->flags.contains(QStringLiteral("\\Seen")));
        if (qEnvironmentVariableIsSet("MAIL_TEST_GRAB")) { // a picture, to look at
            window.resize(1200, 760);
            QTest::qWait(200);
            window.grab().save(qEnvironmentVariable("MAIL_TEST_GRAB"));
        }

        // Reply: to John, "Re:", quoted; sent through SMTP; a copy in Sent; marked answered.
        QAction *reply = nullptr;
        for (QAction *action : window.actions()) {
            if (action->text() == QStringLiteral("Reply"))
                reply = action;
        }
        QVERIFY(reply);
        reply->trigger();
        ComposeWindow *compose = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (auto *c = qobject_cast<ComposeWindow *>(w))
                compose = c;
        }
        QVERIFY(compose);
        const Composer::Draft draft = compose->draft();
        QCOMPARE(draft.to.size(), 1);
        QCOMPARE(draft.to.first().email, QStringLiteral("john@example.org"));
        QCOMPARE(draft.subject, QStringLiteral("Re: Lunch?"));
        QCOMPARE(draft.inReplyTo, QByteArray("lunch@example.org"));
        QVERIFY(draft.text.contains(QStringLiteral("> Shall we eat at one?")));
        compose->findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral("Yes!\n") + draft.text);
        QPushButton *send = nullptr;
        for (QPushButton *b : compose->findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("Send"))
                send = b;
        }
        QVERIFY(send);
        send->click();
        QTRY_VERIFY_WITH_TIMEOUT(smtp.data.contains("Yes!"), 10000);
        QCOMPARE(smtp.to, QList<QByteArray>{"<john@example.org>"});
        QTRY_COMPARE_WITH_TIMEOUT(imap.boxes["Sent"].mails.size(), 1, 10000);
        QTRY_VERIFY(imap.find("INBOX", 1)->flags.contains(QStringLiteral("\\Answered")));
    }
};

MAIL_TEST_MAIN(TestMainWindow, QApplication)
#include "tst_mainwindow.moc"
