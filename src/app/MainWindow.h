#pragma once

#include "MailStore.h"
#include "Theme.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class ComposeWindow;
class FolderTree;
class MessageList;
class MessageView;
class QAction;
class QLabel;
class QLineEdit;
class QSplitter;
class Sync;

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
    Sync *syncFor(const QString &accountId) const { return m_syncs.value(accountId); }
    // Starts (or starts again) an account's sync; tests hand it their own.
    void addSync(Sync *sync);

    ComposeWindow *compose(const QString &accountId = QString());

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
    void applyTheme();
    void loadSettings();
    void saveSettings();

    MailStore *m_store;
    QHash<QString, Sync *> m_syncs;
    FolderTree *m_folders = nullptr;
    MessageList *m_list = nullptr;
    MessageView *m_view = nullptr;
    QSplitter *m_split = nullptr;      // folders | the rest
    QSplitter *m_rightSplit = nullptr; // list over message
    QLineEdit *m_search = nullptr;
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
};
