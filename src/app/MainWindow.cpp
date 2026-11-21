#include "MainWindow.h"

#include "AboutDialog.h"
#include "Account.h"
#include "AccountDialog.h"
#include "CommandPalette.h"
#include "ComposeWindow.h"
#include "Composer.h"
#include "ExtensionMenu.h"
#include "FolderTree.h"
#include "MessageList.h"
#include "MessageView.h"
#include "PreferencesDialog.h"
#include "RecentPopup.h"
#include "StyleSheet.h"
#include "Sync.h"
#include "TrayIcon.h"

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
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTimer>
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
    m_tabs = new QTabBar(right);
    m_tabs->setObjectName(QStringLiteral("categoryTabs"));
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->hide();
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0)
            m_list->setCategory(m_tabs->tabData(index).toString());
    });
    auto *listPane = new QWidget(right);
    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    listLayout->addWidget(m_tabs);
    listLayout->addWidget(m_list, 1);
    m_rightSplit = new QSplitter(Qt::Vertical, right);
    m_rightSplit->addWidget(listPane);
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
    connect(store, &MailStore::folderChanged, this, [this](const QString &account, const QByteArray &path) {
        if (account == m_list->messages()->account() && path == m_list->messages()->folder())
            updateTabs();
    });
    connect(m_folders, &FolderTree::accountChosen, this, [this] {
        m_list->setFolder(QString(), QByteArray(), false);
        updateTabs();
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
    // Right-click on a message: what can be done with it, extensions' commands too.
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (m_list->current().uid == 0)
            return;
        QMenu menu(this);
        for (QAction *a : {m_replyAct, m_replyAllAct, m_forwardAct})
            menu.addAction(a);
        menu.addSeparator();
        for (QAction *a : {m_readAct, m_starAct, m_deleteAct})
            menu.addAction(a);
        QMenu more;
        if (ExtensionMenu::add(&more, this, extensionSelection()) > 0) {
            menu.addSeparator();
            const QList<QAction *> actions = more.actions();
            for (QAction *a : actions) {
                more.removeAction(a);
                a->setParent(&menu);
                if (QMenu *sub = a->menu())
                    sub->setParent(&menu, sub->windowFlags());
                menu.addAction(a);
            }
        }
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    connect(m_list, &MessageList::messageChosen, this, &MainWindow::showMessage);
    connect(m_list->messages(), &QAbstractItemModel::modelReset, this, &MainWindow::updateActions);

    applyTheme();
    loadSettings();
    m_folders->reload();
    startAccounts();
    updateActions();
    connectExtensions();
    buildTray();
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
    connect(m_quitAct, &QAction::triggered, this, &MainWindow::quit);
    addAction(m_quitAct);
    m_paletteAct = new QAction(tr("Command Palette…"), this);
    m_paletteAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    connect(m_paletteAct, &QAction::triggered, this, &MainWindow::showCommandPalette);
    addAction(m_paletteAct);
    m_extensionsAct = make(tr("Extensions…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), &MainWindow::showExtensions);
    m_claudeAct = make(tr("Claude…"), QKeySequence(), &MainWindow::showClaudeSettings);
    m_reportsAct = make(tr("Claude Reports"), QKeySequence(), &MainWindow::openClaudeReports);
    m_prefsAct = make(tr("Preferences…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), &MainWindow::showPreferences);
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
    file->addAction(m_reportsAct);
    file->addSeparator();
    file->addAction(m_quitAct);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_searchAct);
    edit->addAction(m_deleteAct);
    edit->addSeparator();
    edit->addAction(m_extensionsAct);
    edit->addAction(m_claudeAct);
    edit->addSeparator();
    edit->addAction(m_prefsAct);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QMenu *pane = view->addMenu(tr("Message Pane"));
    auto *paneGroup = new QActionGroup(pane);
    m_belowAct = pane->addAction(tr("Below the List"));
    m_besideAct = pane->addAction(tr("Beside the List"));
    for (QAction *a : {m_belowAct, m_besideAct}) {
        a->setCheckable(true);
        paneGroup->addAction(a);
    }
    m_belowAct->setChecked(true);
    connect(m_belowAct, &QAction::triggered, this, [this] { setMessageBeside(false); });
    connect(m_besideAct, &QAction::triggered, this, [this] { setMessageBeside(true); });
    view->addSeparator();
    m_themeMenu = view->addMenu(tr("Theme"));
    connect(m_themeMenu, &QMenu::aboutToShow, this, &MainWindow::fillThemeMenu);
    view->addSeparator();
    m_trayAct = view->addAction(tr("Keep Running in the Tray"));
    m_trayAct->setCheckable(true);
    m_trayAct->setChecked(QSettings().value(QStringLiteral("window/keepInTray"), true).toBool());
    m_trayAct->setToolTip(tr("Closing the window leaves Argonaut Emails in the system tray, checking for mail"));
    connect(m_trayAct, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("window/keepInTray"), on);
        if (m_preferences)
            m_preferences->setKeepInTray(on);
    });
    view->addSeparator();
    view->addAction(m_paletteAct);

    QMenu *message = menuBar()->addMenu(tr("&Message"));
    message->addAction(m_replyAct);
    message->addAction(m_replyAllAct);
    message->addAction(m_forwardAct);
    message->addSeparator();
    message->addAction(m_readAct);
    message->addAction(m_starAct);
    message->addAction(m_deleteAct);

    // Extensions' commands go at the end of these, made again each time one opens.
    m_extensionMenus = {{QStringLiteral("file"), file}, {QStringLiteral("edit"), edit}, {QStringLiteral("view"), view},
                        {QStringLiteral("message"), message}};
    for (auto it = m_extensionMenus.constBegin(); it != m_extensionMenus.constEnd(); ++it) {
        connect(it.value(), &QMenu::aboutToShow, this,
                [this, which = it.key(), menu = it.value()] { ExtensionMenu::addToMenuBar(menu, this, which); });
    }

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About Argonaut Emails"), this, &MainWindow::showAbout);
}

void MainWindow::fillThemeMenu()
{
    m_themeMenu->clear();
    auto *group = new QActionGroup(m_themeMenu);
    const QString current = savedThemeKey();
    for (const ThemeChoice &choice : allThemes()) {
        QAction *a = m_themeMenu->addAction(choice.name);
        a->setCheckable(true);
        a->setChecked(choice.key == current);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [key = choice.key] { setThemeKey(key); });
    }
}

void MainWindow::showCommandPalette()
{
    if (!m_palette)
        m_palette = new CommandPalette(this);
    updateActions();
    refreshExtensionMenus(); // extensions' menu commands are found like the rest
    fillThemeMenu();         // "View › Theme › Forest": the themes there are now

    QList<CommandPalette::Command> commands;
    QSet<QAction *> seen{m_paletteAct};
    auto clean = [](QString text) {
        // "&Open" → "Open", but "Q && A" is "Q & A".
        text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
        text.remove(QLatin1Char('&'));
        text.replace(QLatin1Char('\x01'), QLatin1Char('&'));
        text.remove(QChar(0x2026));
        return text.trimmed();
    };
    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &where) {
        for (QAction *act : menu->actions()) {
            if (act->isSeparator() || !act->isVisible())
                continue;
            if (QMenu *sub = act->menu()) {
                walk(sub, where + QStringLiteral(" › ") + clean(sub->title()));
                continue;
            }
            if (!act->isEnabled() || act->text().isEmpty() || seen.contains(act))
                continue;
            seen.insert(act);
            // An extension's: "ext::Summarize Last Week…", in "File · Last Week with Claude".
            const QString extension = act->property("argonautExtension").toString();
            if (!extension.isEmpty())
                commands.append({Extensions::paletteName(clean(act->text())),
                                 where + QStringLiteral(" · ") + Extensions::Host::instance()->nameOf(extension), act});
            else
                commands.append({clean(act->text()), where, act});
        }
    };
    for (QAction *top : menuBar()->actions()) {
        if (QMenu *menu = top->menu())
            walk(menu, clean(menu->title()));
    }
    m_palette->open(commands);
}

bool MainWindow::keepsRunningInTray() const
{
    return m_tray && m_tray->icon()->isVisible() && m_trayAct && m_trayAct->isChecked();
}

void MainWindow::buildTray()
{
    auto *menu = new QMenu(this);
    menu->addAction(tr("Open Argonaut Emails"), this, &MainWindow::showFromTray);
    menu->addAction(m_writeAct);
    menu->addAction(m_getAct);
    menu->addSeparator();
    menu->addAction(m_quitAct);
    m_tray = new TrayIcon(menu, this);
    m_recent = new RecentPopup(m_store, this);
    connect(m_tray, &TrayIcon::clicked, this, [this](const QRect &where) {
        if (m_recent->isVisible())
            m_recent->hide();
        else
            m_recent->popup(where);
    });
    connect(m_tray, &TrayIcon::openRequested, this, &MainWindow::showFromTray);
    connect(m_recent, &RecentPopup::openRequested, this, &MainWindow::showFromTray);
    connect(m_recent, &RecentPopup::messageChosen, this, &MainWindow::openFromTray);
    // The number follows the store (a little later: a sync changes many things at once).
    auto *later = new QTimer(this);
    later->setSingleShot(true);
    later->setInterval(300);
    connect(later, &QTimer::timeout, this, [this] {
        updateUnread();
        m_recent->reload();
    });
    connect(m_store, &MailStore::folderChanged, later, qOverload<>(&QTimer::start));
    connect(m_store, &MailStore::foldersChanged, later, qOverload<>(&QTimer::start));
    updateUnread();
}

void MainWindow::updateUnread()
{
    if (m_tray)
        m_tray->setUnread(RecentPopup::unread(m_store));
}

void MainWindow::showFromTray()
{
    QApplication::setQuitOnLastWindowClosed(true);
    if (isMinimized())
        showNormal();
    else
        show();
    raise();
    activateWindow();
}

void MainWindow::openFromTray(const MailStore::Summary &summary)
{
    showFromTray();
    m_folders->select(summary.account, summary.folder);
    if (m_list->messages()->account() != summary.account || m_list->messages()->folder() != summary.folder)
        showFolder(summary.account, summary.folder);
    m_list->selectUid(summary.uid);
    m_list->setFocus();
}

void MainWindow::quit()
{
    m_quitting = true;
    if (close())
        QApplication::quit(); // the compose windows too
    else
        m_quitting = false;
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
            addSync(newSync(a));
    }
    if (Account::all().isEmpty())
        m_view->showNote(tr("Add your mail account to start: File → Add Account…"));
}

Sync *MainWindow::newSync(const Account &account)
{
    auto *sync = new Sync(account, m_store, this);
    sync->setPush(PreferencesDialog::push()); // tests' own syncs (addSync) don't push
    return sync;
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
    // Preferences → General (tests keep their own: their settings have neither).
    QSettings settings;
    if (settings.contains(QStringLiteral("mail/checkMinutes")))
        sync->setPollInterval(PreferencesDialog::checkMinutes() * 60 * 1000);
    if (!PreferencesDialog::fetchAhead())
        sync->setPrefetch(0);
    sync->start();
}

void MainWindow::showFolder(const QString &account, const QByteArray &path)
{
    const MailStore::Folder f = m_store->folder(account, path);
    const bool recipients = f.role == QLatin1String("sent") || f.role == QLatin1String("drafts");
    m_list->setCategory(QString());
    m_list->setFolder(account, path, recipients);
    updateTabs();
    m_view->showNote(tr("Choose a message to read it."));
    m_shown = {};
    if (Sync *sync = m_syncs.value(account))
        sync->syncFolder(path);
    setWindowTitle(QStringLiteral("%1 – %2").arg(f.name, tr("Argonaut Emails")));
    updateActions();
}

void MainWindow::updateTabs()
{
    const QString account = m_list->messages()->account();
    const QByteArray path = m_list->messages()->folder();
    const QHash<QString, QPair<int, int>> counts = account.isEmpty() ? QHash<QString, QPair<int, int>>()
                                                                     : m_store->categoryCounts(account, path);
    if (counts.isEmpty()) { // not Gmail's inbox (or not known yet): every message, no tabs
        m_tabs->hide();
        m_list->setCategory(QString());
        return;
    }
    static const QHash<QString, QString> names{
        {QStringLiteral("primary"), QT_TR_NOOP("Primary")},     {QStringLiteral("promotions"), QT_TR_NOOP("Promotions")},
        {QStringLiteral("social"), QT_TR_NOOP("Social")},       {QStringLiteral("updates"), QT_TR_NOOP("Updates")},
        {QStringLiteral("forums"), QT_TR_NOOP("Forums")},
    };
    QString chosen = m_list->messages()->category();
    QStringList shown;
    for (const QString &c : MailStore::categories()) {
        if (c == QLatin1String("primary") || counts.value(c).first > 0)
            shown.append(c);
    }
    if (!shown.contains(chosen))
        chosen = QStringLiteral("primary");
    QSignalBlocker block(m_tabs);
    while (m_tabs->count() > shown.size())
        m_tabs->removeTab(m_tabs->count() - 1);
    for (int i = 0; i < shown.size(); ++i) {
        const QString &c = shown.at(i);
        const int unread = counts.value(c).second;
        const QString name = tr(names.value(c).toUtf8().constData());
        const QString text = unread > 0 ? QStringLiteral("%1  (%2)").arg(name).arg(unread) : name;
        if (i >= m_tabs->count())
            m_tabs->addTab(text);
        else
            m_tabs->setTabText(i, text);
        m_tabs->setTabData(i, c);
        if (c == chosen)
            m_tabs->setCurrentIndex(i);
    }
    m_tabs->show();
    m_list->setCategory(chosen);
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
    addSync(newSync(a));
    m_view->showNote(tr("Choose a message to read it."));
    if (m_preferences)
        m_preferences->reloadAccounts();
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
    editAccountById(chosenAccount());
}

void MainWindow::editAccountById(const QString &id)
{
    const Account a = Account::byId(id);
    if (!a.isValid())
        return;
    AccountDialog dialog(a, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_folders->reload();
    addSync(newSync(dialog.account())); // the new settings
    if (m_preferences)
        m_preferences->reloadAccounts();
}

void MainWindow::removeAccount()
{
    removeAccountById(chosenAccount());
}

void MainWindow::removeAccountById(const QString &id)
{
    const Account a = Account::byId(id);
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
    updateTabs();
    m_view->showNote(Account::all().isEmpty() ? tr("Add your mail account to start: File → Add Account…") : tr("Choose a message to read it."));
    m_folders->reload();
    if (m_preferences)
        m_preferences->reloadAccounts();
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
    if (m_recent) {
        m_recent->setStyleSheet(styleSheetFor(theme)); // a window of its own
        m_recent->applyTheme(theme);
    }
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
    // Qt 6 doesn't hand ApplicationPaletteChange to changeEvent(): the PaletteChange that follows it is
    // what arrives here. applyTheme() does nothing when the theme is the same (its own style sheet).
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        applyTheme();
    QMainWindow::changeEvent(event);
}

namespace {

// Where the list | message divider was, one for each way.
QString splitKey(bool beside)
{
    return beside ? QStringLiteral("window/rightSplitBeside") : QStringLiteral("window/rightSplit");
}

} // namespace

bool MainWindow::messageBeside() const
{
    return m_rightSplit->orientation() == Qt::Horizontal;
}

void MainWindow::setMessageBeside(bool beside)
{
    m_besideAct->setChecked(beside);
    m_belowAct->setChecked(!beside);
    if (m_preferences)
        m_preferences->setMessageBeside(beside);
    if (beside == messageBeside())
        return;
    QSettings s;
    s.setValue(splitKey(messageBeside()), m_rightSplit->saveState()); // the way it was, for coming back
    s.setValue(QStringLiteral("window/messageBeside"), beside);
    m_rightSplit->setOrientation(beside ? Qt::Horizontal : Qt::Vertical);
    m_list->setCards(beside);
    const QByteArray state = s.value(splitKey(beside)).toByteArray();
    if (state.isEmpty() || !m_rightSplit->restoreState(state)) {
        // The first time: the list two fifths, the message the rest.
        const int whole = beside ? m_rightSplit->width() : m_rightSplit->height();
        m_rightSplit->setSizes({whole * 2 / 5, whole - whole * 2 / 5});
    }
}

void MainWindow::loadSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    if (!s.value(QStringLiteral("window/split")).isNull())
        m_split->restoreState(s.value(QStringLiteral("window/split")).toByteArray());
    if (!s.contains(QStringLiteral("window/geometry")))
        resize(1200, 800);
    const bool beside = s.value(QStringLiteral("window/messageBeside"), false).toBool();
    m_besideAct->setChecked(beside);
    m_belowAct->setChecked(!beside);
    m_rightSplit->setOrientation(beside ? Qt::Horizontal : Qt::Vertical);
    m_list->setCards(beside);
    if (!s.value(splitKey(beside)).isNull())
        m_rightSplit->restoreState(s.value(splitKey(beside)).toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/split"), m_split->saveState());
    s.setValue(splitKey(messageBeside()), m_rightSplit->saveState());
    s.setValue(QStringLiteral("window/messageBeside"), messageBeside());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (keepsRunningInTray() && !m_quitting) {
        // As Viber: the window goes, the program stays in the tray and keeps checking for mail.
        saveSettings();
        QApplication::setQuitOnLastWindowClosed(false); // a compose window closed now doesn't end it
        hide();
        event->ignore();
        QSettings settings;
        if (!settings.value(QStringLiteral("window/toldAboutTray")).toBool()) {
            settings.setValue(QStringLiteral("window/toldAboutTray"), true);
            m_tray->showMessage(tr("Argonaut Emails is still running"),
                                tr("It keeps checking for mail in the tray. File → Quit ends it; View → Keep Running in the Tray changes this."));
        }
        return;
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::showAbout()
{
    auto *about = new AboutDialog(this); // deletes itself on closing
    about->open();
}

void MainWindow::showPreferences()
{
    if (m_preferences) {
        m_preferences->raise();
        m_preferences->activateWindow();
        return;
    }
    m_preferences = new PreferencesDialog(this);
    m_preferences->setAttribute(Qt::WA_DeleteOnClose);
    PreferencesDialog *p = m_preferences;
    connect(p, &PreferencesDialog::keepInTrayChanged, m_trayAct, &QAction::setChecked);
    connect(p, &PreferencesDialog::checkMinutesChanged, this, [this](int minutes) {
        for (Sync *sync : std::as_const(m_syncs))
            sync->setPollInterval(minutes * 60 * 1000);
    });
    connect(p, &PreferencesDialog::fetchAheadChanged, this, [this](bool on) {
        for (Sync *sync : std::as_const(m_syncs))
            sync->setPrefetch(on ? Sync::Prefetch : 0);
    });
    connect(p, &PreferencesDialog::pushChanged, this, [this](bool on) {
        for (Sync *sync : std::as_const(m_syncs))
            sync->setPush(on);
    });
    connect(p, &PreferencesDialog::messageBesideChanged, this, &MainWindow::setMessageBeside);
    connect(p, &PreferencesDialog::addAccountRequested, this, &MainWindow::addAccount);
    connect(p, &PreferencesDialog::editAccountRequested, this, &MainWindow::editAccountById);
    connect(p, &PreferencesDialog::removeAccountRequested, this, &MainWindow::removeAccountById);
    connect(p, &PreferencesDialog::claudeRequested, this, [this, p] {
        showClaudeSettings();
        if (m_preferences == p)
            p->reloadClaude();
    });
    connect(p, &PreferencesDialog::extensionsRequested, this, &MainWindow::showExtensions);
    connect(p, &PreferencesDialog::reportsRequested, this, &MainWindow::openClaudeReports);
    p->show();
}
