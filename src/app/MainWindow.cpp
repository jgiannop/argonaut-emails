#include "MainWindow.h"

#include "Account.h"
#include "AccountDialog.h"
#include "ComposeWindow.h"
#include "Composer.h"
#include "FolderTree.h"
#include "MessageList.h"
#include "MessageView.h"
#include "StyleSheet.h"
#include "Sync.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(MailStore *store, QWidget *parent)
    : QMainWindow(parent)
    , m_store(store)
{
    setWindowTitle(tr("Argonaut Emails"));
    setWindowIcon(applicationIcon());

    m_folders = new FolderTree(store, this);
    m_list = new MessageList(store, this);
    m_view = new MessageView(this);

    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    m_search = new QLineEdit(right);
    m_search->setObjectName(QStringLiteral("searchField"));
    m_search->setPlaceholderText(tr("Search the folder's messages (Ctrl+F)"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, m_list, &MessageList::setSearch);
    m_rightSplit = new QSplitter(Qt::Vertical, right);
    m_rightSplit->addWidget(m_list);
    m_rightSplit->addWidget(m_view);
    m_rightSplit->setStretchFactor(0, 2);
    m_rightSplit->setStretchFactor(1, 3);
    rightLayout->addWidget(m_search);
    rightLayout->addWidget(m_rightSplit, 1);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->addWidget(m_folders);
    m_split->addWidget(right);
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({240, 960});
    setCentralWidget(m_split);

    m_status = new QLabel(this);
    statusBar()->addWidget(m_status, 1);

    buildActions();
    buildMenus();
    buildToolBar();

    connect(m_folders, &FolderTree::folderChosen, this, &MainWindow::showFolder);
    connect(m_folders, &FolderTree::accountChosen, this, [this] {
        m_list->setFolder(QString(), QByteArray(), false);
        m_view->showNote(tr("Choose a folder. File → Account Settings… changes this account (its password too)."));
        updateActions();
    });
    // Right-click on an account or its folders: its settings.
    m_folders->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_folders, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_folders->itemAt(pos);
        if (!item)
            return;
        m_folders->setCurrentItem(item);
        QMenu menu(this);
        menu.addAction(m_getAct);
        menu.addSeparator();
        menu.addAction(m_editAccountAct);
        menu.addAction(m_removeAccountAct);
        menu.exec(m_folders->viewport()->mapToGlobal(pos));
    });
    connect(m_list, &MessageList::messageChosen, this, &MainWindow::showMessage);
    connect(m_list->messages(), &QAbstractItemModel::modelReset, this, &MainWindow::updateActions);

    applyTheme();
    loadSettings();
    m_folders->reload();
    startAccounts();
    updateActions();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildActions()
{
    auto make = [this](const QString &text, const QKeySequence &key, void (MainWindow::*slot)()) {
        auto *a = new QAction(text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        return a;
    };
    m_getAct = make(tr("Get Messages"), QKeySequence(Qt::Key_F5), &MainWindow::getMessages);
    m_writeAct = new QAction(tr("Write"), this);
    m_writeAct->setShortcut(QKeySequence::New);
    connect(m_writeAct, &QAction::triggered, this, [this] { compose(); });
    addAction(m_writeAct);
    m_replyAct = new QAction(tr("Reply"), this);
    m_replyAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(m_replyAct, &QAction::triggered, this, [this] { reply(false); });
    addAction(m_replyAct);
    m_replyAllAct = new QAction(tr("Reply All"), this);
    m_replyAllAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    connect(m_replyAllAct, &QAction::triggered, this, [this] { reply(true); });
    addAction(m_replyAllAct);
    m_forwardAct = make(tr("Forward"), QKeySequence(Qt::CTRL | Qt::Key_L), &MainWindow::forward);
    m_deleteAct = make(tr("Delete"), QKeySequence(Qt::Key_Delete), &MainWindow::deleteMessage);
    m_readAct = make(tr("Mark as Unread"), QKeySequence(Qt::Key_M), &MainWindow::toggleRead);
    m_starAct = make(tr("Star"), QKeySequence(Qt::Key_S), &MainWindow::toggleStar);
    m_addAccountAct = make(tr("Add Account…"), QKeySequence(), &MainWindow::addAccount);
    m_editAccountAct = make(tr("Account Settings…"), QKeySequence(), &MainWindow::editAccount);
    m_removeAccountAct = make(tr("Remove Account…"), QKeySequence(), &MainWindow::removeAccount);
    m_quitAct = new QAction(tr("Quit"), this);
    m_quitAct->setShortcut(QKeySequence::Quit);
    connect(m_quitAct, &QAction::triggered, this, &QWidget::close);
    addAction(m_quitAct);
    m_searchAct = new QAction(tr("Search"), this);
    m_searchAct->setShortcut(QKeySequence::Find);
    connect(m_searchAct, &QAction::triggered, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    addAction(m_searchAct);
    // Delete and plain letters only while the list has the keys: elsewhere they are text.
    for (QAction *a : {m_readAct, m_starAct, m_deleteAct}) {
        removeAction(a);
        a->setShortcutContext(Qt::WidgetShortcut);
        m_list->addAction(a);
    }
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_writeAct);
    file->addAction(m_getAct);
    file->addSeparator();
    file->addAction(m_addAccountAct);
    file->addAction(m_editAccountAct);
    file->addAction(m_removeAccountAct);
    file->addSeparator();
    file->addAction(m_quitAct);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_searchAct);
    edit->addAction(m_deleteAct);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QMenu *themes = view->addMenu(tr("Theme"));
    connect(themes, &QMenu::aboutToShow, this, [this, themes] {
        themes->clear();
        auto *group = new QActionGroup(themes);
        const QString current = savedThemeKey();
        for (const ThemeChoice &choice : allThemes()) {
            QAction *a = themes->addAction(choice.name);
            a->setCheckable(true);
            a->setChecked(choice.key == current);
            group->addAction(a);
            connect(a, &QAction::triggered, this, [key = choice.key] { setThemeKey(key); });
        }
    });

    QMenu *message = menuBar()->addMenu(tr("&Message"));
    message->addAction(m_replyAct);
    message->addAction(m_replyAllAct);
    message->addAction(m_forwardAct);
    message->addSeparator();
    message->addAction(m_readAct);
    message->addAction(m_starAct);
    message->addAction(m_deleteAct);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About Argonaut Emails"), this, [this] {
        QMessageBox::about(this, tr("About Argonaut Emails"),
                           tr("<b>Argonaut Emails</b> %1<br>An email client in Qt and C++, from the makers of Argonaut.<br>"
                              "License: GPL-3.0-or-later")
                               .arg(QStringLiteral(MAIL_VERSION)));
    });
}

void MainWindow::buildToolBar()
{
    QToolBar *bar = addToolBar(tr("Mail"));
    bar->setObjectName(QStringLiteral("mailToolBar"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addAction(m_getAct);
    bar->addAction(m_writeAct);
    bar->addSeparator();
    bar->addAction(m_replyAct);
    bar->addAction(m_replyAllAct);
    bar->addAction(m_forwardAct);
    bar->addSeparator();
    bar->addAction(m_deleteAct);
    bar->addAction(m_starAct);
}

void MainWindow::startAccounts()
{
    for (const Account &a : Account::all()) {
        if (!m_syncs.contains(a.id))
            addSync(new Sync(a, m_store, this));
    }
    if (Account::all().isEmpty())
        m_view->showNote(tr("Add your mail account to start: File → Add Account…"));
}

void MainWindow::addSync(Sync *sync)
{
    const QString id = sync->account().id;
    delete m_syncs.take(id);
    sync->setParent(this);
    m_syncs.insert(id, sync);
    connect(sync, &Sync::status, this, [this](const QString &text) { m_status->setText(text); });
    connect(sync, &Sync::error, this, [this](const QString &text) {
        statusBar()->showMessage(text + tr("  (File → Account Settings… to change it)"), 30000);
    });
    sync->start();
}

void MainWindow::showFolder(const QString &account, const QByteArray &path)
{
    const MailStore::Folder f = m_store->folder(account, path);
    const bool recipients = f.role == QLatin1String("sent") || f.role == QLatin1String("drafts");
    m_list->setFolder(account, path, recipients);
    m_view->showNote(tr("Choose a message to read it."));
    m_shown = {};
    if (Sync *sync = m_syncs.value(account))
        sync->syncFolder(path);
    setWindowTitle(QStringLiteral("%1 – %2").arg(f.name, tr("Argonaut Emails")));
    updateActions();
}

void MainWindow::showMessage(const MailStore::Summary &summary)
{
    m_shown = summary;
    updateActions();
    Sync *sync = m_syncs.value(summary.account);
    if (!sync)
        return;
    const quint64 request = ++m_request;
    m_view->showNote(tr("Loading…"));
    sync->fetchBody(summary.folder, summary.uid, [this, request, summary, sync](const QByteArray &raw, const QString &error) {
        if (request != m_request)
            return; // another message was chosen meanwhile
        if (raw.isEmpty()) {
            m_view->showNote(tr("The message couldn't be opened: %1").arg(error));
            return;
        }
        m_view->showMessage(raw);
        sync->setSeen(summary.folder, summary.uid, true);
        updateActions();
    });
}

ComposeWindow *MainWindow::compose(const QString &accountId)
{
    QString id = accountId;
    if (id.isEmpty())
        id = m_folders->currentAccount();
    if (Account::all().isEmpty()) {
        QMessageBox::information(this, tr("Write"), tr("Add your mail account first: File → Add Account…"));
        return nullptr;
    }
    auto *window = new ComposeWindow(id, nullptr);
    connect(window, &ComposeWindow::sent, this, [this](const QString &account, const QByteArray &raw) {
        statusBar()->showMessage(tr("Sent."), 5000);
        if (Sync *sync = m_syncs.value(account))
            sync->keepSent(raw);
    });
    window->show();
    return window;
}

void MainWindow::reply(bool all)
{
    if (!m_view->hasMessage())
        return;
    const Mime::Message &m = m_view->message();
    const MailStore::Summary original = m_shown;
    ComposeWindow *w = compose(original.account);
    if (!w)
        return;
    const Account me = Account::byId(original.account);
    QList<Mime::Address> to = m.replyTo().isEmpty() ? m.from() : m.replyTo();
    QStringList toText;
    for (const Mime::Address &a : to)
        toText.append(a.display());
    w->setTo(toText.join(QStringLiteral(", ")));
    if (all) {
        QStringList cc;
        for (const Mime::Address &a : m.to() + m.cc()) {
            if (a.email.compare(me.email, Qt::CaseInsensitive) == 0)
                continue;
            bool already = false;
            for (const Mime::Address &t : to)
                already = already || t.email.compare(a.email, Qt::CaseInsensitive) == 0;
            if (!already)
                cc.append(a.display());
        }
        w->setCc(cc.join(QStringLiteral(", ")));
    }
    w->setSubject(Composer::replySubject(m.subject()));
    w->setReplyTo(m.messageId(), m.references());
    w->setText(Composer::quoted(m));
    connect(w, &ComposeWindow::sent, this, [this, original] {
        if (Sync *sync = m_syncs.value(original.account))
            sync->setAnswered(original.folder, original.uid);
    });
}

void MainWindow::forward()
{
    if (!m_view->hasMessage())
        return;
    const Mime::Message &m = m_view->message();
    ComposeWindow *w = compose(m_shown.account);
    if (!w)
        return;
    w->setSubject(Composer::forwardSubject(m.subject()));
    w->setText(Composer::forwardedText(m));
    for (const Mime::Attachment &a : m.attachments()) {
        if (a.part)
            w->addFile({a.fileName, a.type, a.part->body});
    }
}

void MainWindow::deleteMessage()
{
    const MailStore::Summary s = m_list->current();
    if (s.uid == 0)
        return;
    Sync *sync = m_syncs.value(s.account);
    if (!sync)
        return;
    sync->moveToTrash(s.folder, s.uid);
    statusBar()->showMessage(tr("Moved to the Trash."), 4000);
}

void MainWindow::toggleRead()
{
    const MailStore::Summary s = m_list->current();
    if (Sync *sync = m_syncs.value(s.account); sync && s.uid != 0)
        sync->setSeen(s.folder, s.uid, !s.seen);
}

void MainWindow::toggleStar()
{
    const MailStore::Summary s = m_list->current();
    if (Sync *sync = m_syncs.value(s.account); sync && s.uid != 0)
        sync->setFlagged(s.folder, s.uid, !s.flagged);
}

void MainWindow::getMessages()
{
    for (Sync *sync : std::as_const(m_syncs))
        sync->syncAll();
}

void MainWindow::addAccount()
{
    AccountDialog dialog(Account(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const Account a = dialog.account();
    m_folders->reload();
    addSync(new Sync(a, m_store, this));
    m_view->showNote(tr("Choose a message to read it."));
}

QString MainWindow::chosenAccount() const
{
    const QString id = m_folders->currentAccount();
    if (!id.isEmpty())
        return id;
    const QList<Account> all = Account::all();
    return all.size() == 1 ? all.first().id : QString();
}

void MainWindow::editAccount()
{
    const Account a = Account::byId(chosenAccount());
    if (!a.isValid())
        return;
    AccountDialog dialog(a, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_folders->reload();
    addSync(new Sync(dialog.account(), m_store, this)); // the new settings
}

void MainWindow::removeAccount()
{
    const Account a = Account::byId(chosenAccount());
    if (!a.isValid())
        return;
    if (QMessageBox::question(this, tr("Remove Account"),
                              tr("Remove %1 from Argonaut Emails?\n\nThe mail stays on the server; the copy on this computer and the password are removed.")
                                  .arg(a.email),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes)
        return;
    delete m_syncs.take(a.id);
    Account::remove(a.id);
    m_store->removeAccount(a.id);
    m_list->setFolder(QString(), QByteArray(), false);
    m_view->showNote(Account::all().isEmpty() ? tr("Add your mail account to start: File → Add Account…") : tr("Choose a message to read it."));
    m_folders->reload();
}

void MainWindow::updateActions()
{
    const MailStore::Summary s = m_list->current();
    const bool chosen = s.uid != 0;
    const bool open = m_view->hasMessage();
    m_replyAct->setEnabled(open);
    m_replyAllAct->setEnabled(open);
    m_forwardAct->setEnabled(open);
    m_deleteAct->setEnabled(chosen);
    m_readAct->setEnabled(chosen);
    m_starAct->setEnabled(chosen);
    m_readAct->setText(chosen && !s.seen ? tr("Mark as Read") : tr("Mark as Unread"));
    m_starAct->setText(chosen && s.flagged ? tr("Remove Star") : tr("Star"));
    const bool account = !chosenAccount().isEmpty();
    m_editAccountAct->setEnabled(account);
    m_removeAccountAct->setEnabled(account);
}

void MainWindow::applyTheme()
{
    const Theme theme = currentTheme();
    if (theme.id == m_themeId)
        return;
    m_themeId = theme.id;
    setStyleSheet(styleSheetFor(theme));
    m_folders->applyTheme(theme);
    m_view->applyTheme(theme);
    m_getAct->setIcon(glyphIcon(Glyph::Recent, theme.fg));
    m_writeAct->setIcon(glyphIcon(Glyph::Documents, theme.fg));
    m_replyAct->setIcon(glyphIcon(Glyph::ChevronLeft, theme.fg));
    m_replyAllAct->setIcon(glyphIcon(Glyph::ChevronLeft, theme.fg));
    m_forwardAct->setIcon(glyphIcon(Glyph::ChevronRight, theme.fg));
    m_deleteAct->setIcon(glyphIcon(Glyph::Trash, theme.fg));
    m_starAct->setIcon(glyphIcon(Glyph::Star, theme.fg));
    m_searchAct->setIcon(glyphIcon(Glyph::Search, theme.dimFg));
    m_search->addAction(m_searchAct, QLineEdit::LeadingPosition);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange)
        applyTheme();
    QMainWindow::changeEvent(event);
}

void MainWindow::loadSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    if (!s.value(QStringLiteral("window/split")).isNull())
        m_split->restoreState(s.value(QStringLiteral("window/split")).toByteArray());
    if (!s.value(QStringLiteral("window/rightSplit")).isNull())
        m_rightSplit->restoreState(s.value(QStringLiteral("window/rightSplit")).toByteArray());
    if (!s.contains(QStringLiteral("window/geometry")))
        resize(1200, 800);
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/split"), m_split->saveState());
    s.setValue(QStringLiteral("window/rightSplit"), m_rightSplit->saveState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}
