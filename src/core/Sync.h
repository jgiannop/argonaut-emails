#pragma once

#include "Account.h"
#include "ImapClient.h"
#include "MailStore.h"

#include <QObject>
#include <QTimer>

#include <functional>

// Keeps one account's folders and messages in the store up to date, and
// does what the user asks of the server (open a message, mark it read,
// move it to the Trash, keep a copy of what was sent).
//
// One IMAP connection; what it does is a queue of operations, one at a
// time, because each starts by selecting its folder. A folder is first
// read as its newest `FirstFetch` messages, then only what is new, and the
// flags of what is kept are looked at again (which is also how messages
// deleted elsewhere go). It looks again every five minutes.
class Sync : public QObject
{
    Q_OBJECT

public:
    static constexpr int FirstFetch = 500;

    Sync(const Account &account, MailStore *store, QObject *parent = nullptr);

    const Account &account() const { return m_account; }
    // Tests: the client's settings instead of the account's (and the keyring's password).
    void setSettings(const ImapClient::Settings &settings) { m_settings = settings; m_haveSettings = true; }
    void setPollInterval(int ms);

    // Folders, then the inbox.
    void start();
    void syncFolder(const QByteArray &path);
    void syncAll(); // the folders list and every folder that was looked at

    using BodyDone = std::function<void(const QByteArray &raw, const QString &error)>;
    // From the store when it was opened before, else from the server (kept).
    void fetchBody(const QByteArray &path, qint64 uid, BodyDone done);
    void setSeen(const QByteArray &path, qint64 uid, bool seen);
    void setFlagged(const QByteArray &path, qint64 uid, bool flagged);
    void setAnswered(const QByteArray &path, qint64 uid);
    // To the account's Trash; in the Trash (or with none) it is only marked deleted.
    void moveToTrash(const QByteArray &path, qint64 uid);
    // A copy of a message just sent, in the Sent folder (not for Gmail, which keeps one itself).
    void keepSent(const QByteArray &raw);

    bool isBusy() const { return m_running || !m_ops.isEmpty(); }
    QByteArray folderWithRole(const QString &role) const;

signals:
    void status(const QString &text); // "Checking Inbox…", empty when done
    void error(const QString &text);
    void idle(); // nothing left to do (the tests wait for it)

private:
    struct Op
    {
        QString what; // for the status line
        std::function<void(std::function<void()> next)> run;
        std::function<void(const QString &error)> failed;
    };

    void enqueue(Op op);
    void runNext();
    void connectFailed(const QString &message);
    void select(const QByteArray &path, std::function<void(bool ok, const ImapClient::Result &result)> done);
    void listFolders(std::function<void()> next);
    void readFolder(const QByteArray &path, std::function<void()> next);
    void storeFlag(const QByteArray &path, qint64 uid, const QByteArray &flag, bool on);

    Account m_account;
    MailStore *m_store;
    ImapClient m_client;
    ImapClient::Settings m_settings;
    bool m_haveSettings = false;
    QList<Op> m_ops;
    bool m_running = false;
    bool m_opening = false;
    QByteArray m_selected;
    QSet<QByteArray> m_looked; // folders read since the start
    QTimer m_poll;
};
