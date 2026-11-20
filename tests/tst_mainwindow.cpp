#include "Account.h"
#include "CommandPalette.h"
#include "ComposeWindow.h"
#include "Extensions.h"
#include "FakeServers.h"
#include "FolderTree.h"
#include "MailWebView.h"
#include "MainWindow.h"
#include "MessageList.h"
#include "MessageView.h"
#include "Passwords.h"
#include "Sync.h"
#include "TestSupport.h"

#include <QApplication>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabBar>
#include <QTest>
#include <QTreeWidget>

// Before the application (MAIL_TEST_MAIN makes it): WebEngine's scheme; no GPU offscreen.
static const bool webEnginePrepared = (qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu"), MailWebView::prepare(), true);

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
        QVERIFY(!window.tabs()->isVisible()); // not Gmail: no tabs

        window.list()->setCurrentIndex(window.list()->model()->index(0, 0));
        QTRY_VERIFY(window.view()->hasMessage());
        QTRY_VERIFY_WITH_TIMEOUT(window.view()->shownText().contains(QStringLiteral("Shall we eat at one?")), 10000);
        QVERIFY(!window.view()->shownText().contains(QStringLiteral("bad()")));
        QVERIFY(!window.view()->picturesShown()); // the tracker's picture waits for a click
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

    void gmailTabsOverTheInbox()
    {
        for (const Account &old : Account::all())
            Account::remove(old.id);
        FakeImap imap;
        imap.capabilities += " X-GM-EXT-1";
        imap.add("INBOX", "From: Anna <anna@example.org>\r\nSubject: Dinner\r\nDate: Tue, 1 Jul 2025 10:00:00 +0200\r\n\r\nhi\r\n");
        imap.add("INBOX", "From: Shop <shop@example.org>\r\nSubject: Sale\r\nDate: Tue, 1 Jul 2025 11:00:00 +0200\r\n\r\nbuy\r\n");
        imap.add("INBOX", "From: Bot <bot@example.org>\r\nSubject: Build failed\r\nDate: Tue, 1 Jul 2025 12:00:00 +0200\r\n\r\nred\r\n");
        imap.add("INBOX", "From: Bank <bank@example.org>\r\nSubject: Security alert\r\nDate: Tue, 1 Jul 2025 09:00:00 +0200\r\n\r\n!\r\n");
        imap.boxes["INBOX"].mails[1].category = "promotions";
        imap.boxes["INBOX"].mails[2].category = "updates";
        imap.boxes["INBOX"].mails[3].category = "updates,primary"; // Gmail shows it in both
        imap.boxes.remove("Sent");
        imap.boxes["[Gmail]"].role = "\\Noselect";
        imap.boxes["[Gmail]/Sent Mail"].role = "\\Sent";
        imap.boxes["[Gmail]/Important"];
        Account a;
        a.email = QStringLiteral("maria@example.com");
        a.imapHost = QStringLiteral("127.0.0.1");
        a.imapPort = imap.port();
        a.imapSecurity = ImapClient::Security::None;
        a.smtpHost = QStringLiteral("127.0.0.1");
        Account::save(a);
        QVERIFY(Passwords::save(a.id, QStringLiteral("imap"), imap.password, a.email));
        MailStore store(QDir::homePath() + QStringLiteral("/tabs.sqlite"));
        MainWindow window(&store);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(!window.syncFor(a.id)->isBusy() && window.tabs()->isVisible(), 10000);
        // Primary, Promotions, Updates (no Social, no Forums: nothing there); Primary first, as in Gmail.
        QCOMPARE(window.tabs()->count(), 3);
        QCOMPARE(window.tabs()->currentIndex(), 0);
        QCOMPARE(window.tabs()->tabText(0), QStringLiteral("Primary  (2)"));
        QCOMPARE(window.tabs()->tabText(2), QStringLiteral("Updates  (2)"));
        QCOMPARE(window.list()->messages()->rowCount(), 2);
        QCOMPARE(window.list()->messages()->summaryAt(0).subject, QStringLiteral("Dinner"));
        QCOMPARE(window.list()->messages()->summaryAt(1).subject, QStringLiteral("Security alert"));
        window.tabs()->setCurrentIndex(1);
        QCOMPARE(window.list()->messages()->rowCount(), 1);
        QCOMPARE(window.list()->messages()->summaryAt(0).subject, QStringLiteral("Sale"));
        if (qEnvironmentVariableIsSet("MAIL_TEST_GRAB_TABS")) {
            window.resize(1200, 760);
            QTest::qWait(200);
            window.grab().save(qEnvironmentVariable("MAIL_TEST_GRAB_TABS"));
        }

        // Gmail's Sent Mail right under the account, not hidden inside "[Gmail]";
        // and "[Gmail]" open, with what is left in it.
        QTreeWidgetItem *account = window.folders()->topLevelItem(0);
        QStringList shown;
        for (int i = 0; i < account->childCount(); ++i)
            shown.append(account->child(i)->toolTip(0));
        QVERIFY2(shown.contains(QStringLiteral("[Gmail]/Sent Mail")), qPrintable(shown.join(u',')));
        for (int i = 0; i < account->childCount(); ++i) {
            if (account->child(i)->toolTip(0) == QLatin1String("[Gmail]")) {
                QCOMPARE(account->child(i)->childCount(), 1); // Important
                QVERIFY(account->child(i)->isExpanded());
            }
        }

        // Another folder: no tabs, all of it.
        imap.add("[Gmail]/Sent Mail", "From: maria@example.com\r\nSubject: Hello\r\n\r\nhi\r\n");
        window.folders()->select(a.id, "[Gmail]/Sent Mail");
        QVERIFY(!window.tabs()->isVisible());
        // Back to the Inbox: Primary again.
        window.folders()->select(a.id, "INBOX");
        QVERIFY(window.tabs()->isVisible());
        QCOMPARE(window.tabs()->currentIndex(), 0);
        QCOMPARE(window.list()->messages()->rowCount(), 2);
    }

    void messageBesideTheList()
    {
        for (const Account &old : Account::all())
            Account::remove(old.id);
        MailStore store(QDir::homePath() + QStringLiteral("/beside.sqlite"));
        {
            MainWindow window(&store);
            window.resize(1200, 800);
            window.show();
            QSplitter *split = window.listAndMessage();
            QCOMPARE(split->orientation(), Qt::Vertical); // as before: the message under the list
            split->setSizes({200, 500});
            const QList<int> below = split->sizes();

            window.besideAction()->trigger();
            QCOMPARE(split->orientation(), Qt::Horizontal);
            QCOMPARE(split->widget(0)->isAncestorOf(window.list()), true); // the list at the left
            QCOMPARE(split->widget(1), static_cast<QWidget *>(window.view())); // the message at the right
            QVERIFY(split->sizes().at(1) > split->sizes().at(0)); // the message gets the most
            // The list in two lines a message: no columns, no header.
            QVERIFY(window.list()->cards());
            QVERIFY(window.list()->header()->isHidden());
            QVERIFY(window.list()->isColumnHidden(MessageModel::SubjectColumn));
            QVERIFY(!window.list()->isColumnHidden(MessageModel::PeopleColumn));
            split->setSizes({300, 600});
            const QList<int> beside = split->sizes();

            // Back and forth: each way keeps its own sizes.
            for (QAction *a : window.findChildren<QAction *>()) {
                if (a->text() == QStringLiteral("Below the List"))
                    a->trigger();
            }
            QCOMPARE(split->orientation(), Qt::Vertical);
            QCOMPARE(split->sizes(), below);
            QVERIFY(!window.list()->cards()); // the columns again
            QVERIFY(!window.list()->header()->isHidden());
            QVERIFY(!window.list()->isColumnHidden(MessageModel::SubjectColumn));
            window.besideAction()->trigger();
            QCOMPARE(split->sizes(), beside);
            window.close(); // saves
        }
        // A new window: as it was left.
        MainWindow again(&store);
        again.resize(1200, 800);
        QCOMPARE(again.listAndMessage()->orientation(), Qt::Horizontal);
        QVERIFY(again.list()->cards());
        QVERIFY(again.besideAction()->isChecked());
        for (QAction *a : again.findChildren<QAction *>()) { // the other tests want it below
            if (a->text() == QStringLiteral("Below the List"))
                a->trigger();
        }
        again.close();
    }

    // Ctrl+Shift+P: every menu command, an extension's too, found by a few letters.
    void commandPalette()
    {
        for (const Account &old : Account::all())
            Account::remove(old.id);
        qputenv("MAIL_EXTENSIONS_PATH", MAIL_BUILT_EXTENSIONS);
        Extensions::Host::instance()->rescan();
        Extensions::Host::instance()->setEnabled(QStringLiteral("last-week"), true);
        MailStore store(QDir::homePath() + QStringLiteral("/palette.sqlite"));
        MainWindow window(&store);
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.showCommandPalette();
        CommandPalette *palette = window.palette();
        QVERIFY(palette && palette->isVisible());
        QHash<QString, QString> where;
        for (const CommandPalette::Command &c : palette->commands())
            where.insert(c.name, c.where);
        QCOMPARE(where.value(QStringLiteral("Write")), QStringLiteral("File"));
        QCOMPARE(where.value(QStringLiteral("Extensions")), QStringLiteral("Edit"));
        QCOMPARE(where.value(QStringLiteral("Beside the List")), QStringLiteral("View › Message Pane"));
        QCOMPARE(where.value(QStringLiteral("ext::Summarize Last Week with Claude")), QStringLiteral("File · Last Week with Claude"));
        QVERIFY(!where.contains(QStringLiteral("Command Palette")));

        auto *edit = palette->findChild<QLineEdit *>(QStringLiteral("paletteEdit"));
        auto *list = palette->findChild<QListWidget *>(QStringLiteral("paletteList"));
        QTest::keyClicks(edit, QStringLiteral("besid"));
        QCOMPARE(list->currentItem()->text(), QStringLiteral("Beside the List"));
        QTest::keyClick(edit, Qt::Key_Return);
        QVERIFY(!palette->isVisible());
        QTRY_VERIFY(window.besideAction()->isChecked());
        window.showCommandPalette();
        QTest::keyClick(palette->findChild<QLineEdit *>(QStringLiteral("paletteEdit")), Qt::Key_Escape);
        QVERIFY(!palette->isVisible());
        for (QAction *a : window.findChildren<QAction *>()) {
            if (a->text() == QStringLiteral("Below the List"))
                a->trigger();
        }
        Extensions::Host::instance()->setEnabled(QStringLiteral("last-week"), false);
        qunsetenv("MAIL_EXTENSIONS_PATH");
    }

    // An extension's report: in the message pane, as Markdown made safe.
    void extensionResult()
    {
        for (const Account &old : Account::all())
            Account::remove(old.id);
        MailStore store(QDir::homePath() + QStringLiteral("/result.sqlite"));
        MainWindow window(&store);
        window.resize(1200, 800);
        window.show();
        Extensions::Result result;
        result.kind = Extensions::Result::Markdown;
        result.extension = QStringLiteral("last-week");
        result.title = QStringLiteral("Last week in your mail");
        result.text = QStringLiteral("# Your week\n\n**Eleni** – invoice 42\n\n<script>document.title='ran'</script>");
        result.note = QStringLiteral("By Claude Opus 5.5 from 3 messages");
        emit Extensions::Host::instance()->showRequested(result);
        QVERIFY(window.view()->hasResult());
        QTRY_VERIFY_WITH_TIMEOUT(window.view()->shownText().contains(QStringLiteral("Eleni – invoice 42")), 10000);
        QVERIFY(!window.view()->shownText().contains(QStringLiteral("document.title")));
        QStringList labels;
        for (QLabel *l : window.view()->findChildren<QLabel *>())
            labels << l->text();
        QVERIFY(labels.contains(QStringLiteral("Last week in your mail")));
        QVERIFY(labels.contains(QStringLiteral("By Claude Opus 5.5 from 3 messages")));
    }

    void themeChangesAtOnce()
    {
        MailStore store(QDir::homePath() + QStringLiteral("/theme.sqlite"));
        MainWindow window(&store);
        window.show();
        // The palette's events are posted: a moment, not a restart.
        setThemeKey(QStringLiteral("light"));
        QTRY_VERIFY(window.styleSheet().contains(cssColor(Theme::forMode(ThemeMode::Light).window)));
        setThemeKey(QStringLiteral("dark"));
        QTRY_VERIFY(window.styleSheet().contains(cssColor(Theme::forMode(ThemeMode::Dark).window)));
        setThemeKey(QStringLiteral("system"));
    }

    void wrongPasswordCanBeFixed()
    {
        for (const Account &old : Account::all())
            Account::remove(old.id);
        FakeImap imap;
        Account a;
        a.email = QStringLiteral("maria@example.com");
        a.imapHost = QStringLiteral("127.0.0.1");
        a.imapPort = imap.port();
        a.imapSecurity = ImapClient::Security::None;
        a.smtpHost = QStringLiteral("127.0.0.1");
        Account::save(a);
        QVERIFY(Passwords::save(a.id, QStringLiteral("imap"), QStringLiteral("wrong"), a.email));
        MailStore store(QDir::homePath() + QStringLiteral("/wrong.sqlite"));
        MainWindow window(&store);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(!window.syncFor(a.id)->isBusy(), 10000);
        QVERIFY(store.folders(a.id).isEmpty()); // never logged in
        QAction *settings = nullptr;
        for (QAction *action : window.actions()) {
            if (action->text() == QStringLiteral("Account Settings…"))
                settings = action;
        }
        QVERIFY(settings);
        QVERIFY(settings->isEnabled()); // the one account, though nothing is chosen
        // Its row can be chosen too.
        window.folders()->setCurrentItem(window.folders()->topLevelItem(0));
        QCOMPARE(window.folders()->currentAccount(), a.id);
        QVERIFY(settings->isEnabled());
    }
};

MAIL_TEST_MAIN(TestMainWindow, QApplication)
#include "tst_mainwindow.moc"
