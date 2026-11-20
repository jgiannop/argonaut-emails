#pragma once

#include "Extensions.h"
#include "MailStore.h"
#include "Theme.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class CommandPalette;
class ComposeWindow;
class FolderTree;
class MessageList;
class MessageView;
class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QSplitter;
class QTabBar;
class RecentPopup;
class Sync;
class TrayIcon;

// The three panes, as in Thunderbird: accounts and folders at the left,
// the folder's messages at the top right, the message under them.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(MailStore *store, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Tests reach the parts.
    FolderTree *folders() const { return m_folders; }
    MessageList *list() const { return m_list; }
    MessageView *view() const { return m_view; }
    QSplitter *listAndMessage() const { return m_rightSplit; }
    QAction *besideAction() const { return m_besideAct; }
    QTabBar *tabs() const { return m_tabs; }
    Sync *syncFor(const QString &accountId) const { return m_syncs.value(accountId); }
    // Starts (or starts again) an account's sync; tests hand it their own.
    void addSync(Sync *sync);

    ComposeWindow *compose(const QString &accountId = QString());
    // Ctrl+Shift+P: every menu command, extensions' too (Argonaut's palette).
    void showCommandPalette();
    CommandPalette *palette() const { return m_palette; }
    TrayIcon *tray() const { return m_tray; }
    RecentPopup *recent() const { return m_recent; }
    // Opens a message from the tray's popup: the window comes back, at it.
    void openFromTray(const MailStore::Summary &summary);
    void showFromTray();
    // File → Quit: really, even with the tray (closing the window only hides it then).
    void quit();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void startAccounts();
    void showFolder(const QString &account, const QByteArray &path);
    void showMessage(const MailStore::Summary &summary);
    void updateTabs(); // Gmail's inbox tabs over the list, when the folder has them
    void reply(bool all);
    void forward();
    void deleteMessage();
    void toggleRead();
    void toggleStar();
    void getMessages();
    void addAccount();
    void editAccount();
    void removeAccount();
    void updateActions();
    QString chosenAccount() const; // the one chosen in the tree, else the only one
    void applyTheme();
    // The message under the list (as in 0.1), or beside it.
    void setMessageBeside(bool beside);
    bool messageBeside() const;
    void loadSettings();
    void saveSettings();
    void fillThemeMenu();
    void buildTray();
    void updateUnread(); // the tray's and the dock's number
    bool keepsRunningInTray() const;
    // Extensions (MainWindowExtensions.cpp): their commands in the menus,
    // their results in the message pane, their forms over the window.
    void connectExtensions();
    void refreshExtensionMenus();
    QJsonArray extensionSelection() const; // the chosen message, as told to an extension
    void showExtensionResult(const Extensions::Result &result);
    void showExtensionForm(const Extensions::Form &form);
    void showClaudeWaiting(const QString &extension, const QString &name, int waiting);
    void showExtensions();
    void showClaudeSettings();
    void openClaudeReports();

    MailStore *m_store;
    QHash<QString, Sync *> m_syncs;
    FolderTree *m_folders = nullptr;
    MessageList *m_list = nullptr;
    MessageView *m_view = nullptr;
    QSplitter *m_split = nullptr;      // folders | the rest
    QSplitter *m_rightSplit = nullptr; // list over message
    QLineEdit *m_search = nullptr;
    QTabBar *m_tabs = nullptr; // Primary, Promotions, Social, Updates, Forums
    QLabel *m_status = nullptr;
    QString m_themeId;
    MailStore::Summary m_shown; // the message in the view
    quint64 m_request = 0;      // drops a body that comes after another was chosen

    QAction *m_getAct = nullptr;
    QAction *m_writeAct = nullptr;
    QAction *m_replyAct = nullptr;
    QAction *m_replyAllAct = nullptr;
    QAction *m_forwardAct = nullptr;
    QAction *m_deleteAct = nullptr;
    QAction *m_readAct = nullptr;
    QAction *m_starAct = nullptr;
    QAction *m_addAccountAct = nullptr;
    QAction *m_editAccountAct = nullptr;
    QAction *m_removeAccountAct = nullptr;
    QAction *m_quitAct = nullptr;
    QAction *m_searchAct = nullptr;
    QAction *m_belowAct = nullptr;  // View → Message Pane
    QAction *m_besideAct = nullptr;
    QAction *m_paletteAct = nullptr;
    QAction *m_extensionsAct = nullptr;
    QAction *m_claudeAct = nullptr;
    QAction *m_reportsAct = nullptr;
    QMenu *m_themeMenu = nullptr;
    QHash<QString, QMenu *> m_extensionMenus; // "file", "edit", "view", "message"
    CommandPalette *m_palette = nullptr;
    TrayIcon *m_tray = nullptr;
    RecentPopup *m_recent = nullptr;
    QAction *m_trayAct = nullptr; // View → Keep Running in the Tray
    bool m_quitting = false;
};
