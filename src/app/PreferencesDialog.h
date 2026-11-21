#pragma once

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QSplitter;
class QStackedWidget;

// Edit → Preferences… (Ctrl+,): the sections in a list at the left, the
// chosen one at the right, a divider between them that can be dragged (where
// it is and the last section are kept). Changes apply at once, as in GNOME
// apps and Argonaut: no OK or Cancel.
//
// General: keep running in the tray, new mail at once (push), how often to look again, the text
// of the newest messages fetched ahead. Appearance: the themes and where the
// message pane goes, each drawn small. Accounts: the list, with Add,
// Settings and Remove. Claude & Extensions: the key, the model and what was
// spent, with the buttons to the windows that change them.
//
// The window does the changes through its signals (MainWindow connects them).
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    enum Page { General, Appearance, Accounts, ClaudePage, PageCount };

    explicit PreferencesDialog(QWidget *parent = nullptr);

    void showPage(int page);
    // After an account was added, changed or removed.
    void reloadAccounts();
    // After the Claude window was closed (the key or the model may be new).
    void reloadClaude();
    // When View → Message Pane or the tray item was changed elsewhere.
    void setMessageBeside(bool beside);
    void setKeepInTray(bool keep);

    // Tests reach the parts.
    QListWidget *sections() const { return m_sections; }
    QStackedWidget *pages() const { return m_pages; }
    QSplitter *splitter() const { return m_splitter; }
    QListWidget *accountList() const { return m_accounts; }
    QComboBox *checkEvery() const { return m_checkEvery; }
    QCheckBox *trayCheck() const { return m_tray; }
    QCheckBox *fetchAheadCheck() const { return m_fetchAhead; }
    QCheckBox *pushCheck() const { return m_push; }
    QButtonGroup *paneGroup() const { return m_pane; }

    // The settings it keeps, read where they are used.
    static int checkMinutes();     // "mail/checkMinutes", 5
    static bool fetchAhead();      // "mail/fetchAhead", true
    static bool push();            // "mail/push", true: IMAP IDLE

signals:
    void keepInTrayChanged(bool keep);
    void checkMinutesChanged(int minutes);
    void fetchAheadChanged(bool on);
    void pushChanged(bool on);
    void messageBesideChanged(bool beside);
    void addAccountRequested();
    void editAccountRequested(const QString &accountId);
    void removeAccountRequested(const QString &accountId);
    void claudeRequested();
    void extensionsRequested();
    void reportsRequested();

protected:
    void done(int result) override;
    void changeEvent(QEvent *event) override; // a new theme: the icons and pictures again

private:
    void paintIcons();
    QWidget *buildGeneral();
    QWidget *buildAppearance();
    QWidget *buildAccounts();
    QWidget *buildClaude();
    QString chosenAccount() const;

    QListWidget *m_sections = nullptr;
    QStackedWidget *m_pages = nullptr;
    QSplitter *m_splitter = nullptr;
    QCheckBox *m_tray = nullptr;
    QComboBox *m_checkEvery = nullptr;
    QCheckBox *m_fetchAhead = nullptr;
    QCheckBox *m_push = nullptr;
    QButtonGroup *m_pane = nullptr;
    QListWidget *m_accounts = nullptr;
    QLabel *m_noAccounts = nullptr;
    QLabel *m_claudeKey = nullptr;
    QLabel *m_claudeModel = nullptr;
    QLabel *m_claudeSpent = nullptr;
};
