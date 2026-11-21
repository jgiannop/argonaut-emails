#pragma once

#include "Account.h"
#include "ImapClient.h"
#include "MailStore.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

// Keeps one account's folders and messages in the store up to date, and
// does what the user asks of the server (open a message, mark it read,
// move it to the Trash, keep a copy of what was sent).
//
// One IMAP connection for the folders: what it does is a queue of
// operations, one at a time, because each starts by selecting its folder.
// A second one only opens messages, so a message never waits for a folder
// check. A connection that was quiet for a while is asked first whether it
// is still there (after a sleep, a change of network): a dead one is
// replaced at once, and what waited goes on, instead of a minute's wait.
// After each folder check the newest messages' text is fetched, so most
// messages open from the store. A folder is first
// read as its newest `FirstFetch` messages, then only what is new, and the
// flags of what is kept are looked at again (which is also how messages
// deleted elsewhere go). It looks again every five minutes; with push on
// (setPush), a third connection waits in the inbox with IDLE and the server
// tells at once of new mail, which is then read (the inbox in seconds, the
// other folders still at each look). On Gmail it
// also asks which inbox messages are in which of its tabs (Promotions…).
class Sync : public QObject
{
    Q_OBJECT

public:
    static constexpr int FirstFetch = 500;
    static constexpr int Prefetch = 30;                       // newest messages a folder check fetches the text of
    static constexpr qint64 PrefetchMaxBytes = 512 * 1024;    // not bigger ones (attachments): those when opened

    Sync(const Account &account, MailStore *store, QObject *parent = nullptr);

    const Account &account() const { return m_account; }
    // Tests: the client's settings instead of the account's (and the keyring's password).
    void setSettings(const ImapClient::Settings &settings) { m_settings = settings; m_haveSettings = true; }
    void setPollInterval(int ms);
    // Tests: how many messages a check fetches the text of (0: none); when a
    // quiet connection is checked, how long the check may take, and how long
    // the message connection waits for an answer.
    void setPrefetch(int count) { m_prefetch = count; }
    void setTimeouts(int probeAfterMs, int probeTimeoutMs, int readerTimeoutMs);

    // Push: new mail in the inbox at once (IMAP IDLE), when the server can.
    // Off by default (tests that count connections and commands); the window
    // switches it on from Preferences → General. Without IDLE nothing
    // changes: the five-minute look stays.
    void setPush(bool on);
    bool isPushing() const { return m_push.isIdling(); } // waiting in the inbox now
    // Tests: how often the IDLE is renewed, how long its end may take (else
    // the line is dead), the first wait before connecting again (it doubles,
    // up to five minutes).
    void setPushTimings(int renewMs, int answerMs, int retryMs);

    // Folders, then the inbox.
    void start();
    void syncFolder(const QByteArray &path);
    void syncAll(); // the folders list and every folder that was looked at

    using BodyDone = std::function<void(const QByteArray &raw, const QString &error)>;
    // From the store when it was opened before, else from the server (kept).
    void fetchBody(const QByteArray &path, qint64 uid, BodyDone done);
    // Several messages at once (an extension reads them), kept in the store
    // as they come, in batches; `done` once, with the first error if any.
    void fetchBodies(const QByteArray &path, const QList<qint64> &uids, std::function<void(const QString &error)> done);
    void setSeen(const QByteArray &path, qint64 uid, bool seen);
    void setFlagged(const QByteArray &path, qint64 uid, bool flagged);
    void setAnswered(const QByteArray &path, qint64 uid);
    // To the account's Trash; in the Trash (or with none) it is only marked deleted.
    void moveToTrash(const QByteArray &path, qint64 uid);
    // A copy of a message just sent, in the Sent folder (not for Gmail, which keeps one itself).
    void keepSent(const QByteArray &raw);

    bool isBusy() const { return m_running || !m_ops.isEmpty() || m_reading || !m_reads.isEmpty(); }
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
        bool urgent = false; // someone waits for it (a message being opened): before the folder checks
    };

    void enqueue(Op op);
    void runNext();
    void connectFailed(const QString &message);
    // The connection that opens messages.
    void runReads();
    void readOne();
    void finishRead(const QByteArray &raw, const QString &error);
    void readerFailed(const QString &message);
    bool isQuiet(const ImapClient &client) const;
    ImapClient::Settings settings() const { return m_haveSettings ? m_settings : m_account.imapSettings(); }
    void prefetch(const QByteArray &path, std::function<void()> next);
    void select(const QByteArray &path, std::function<void(bool ok, const ImapClient::Result &result)> done);
    void listFolders(std::function<void()> next);
    void readFolder(const QByteArray &path, std::function<void()> next);
    // Gmail: which of the inbox's messages are in which tab (X-GM-RAW "category:…").
    void readCategories(const QByteArray &path, std::function<void()> next);
    void readCategory(const QByteArray &path, int i, std::shared_ptr<QHash<qint64, QStringList>> found, std::function<void()> next);
    void fetchBatch(const QByteArray &path, std::shared_ptr<QList<qint64>> left, std::function<void(const QString &error)> done,
                    std::function<void()> next);
    void storeFlag(const QByteArray &path, qint64 uid, const QByteArray &flag, bool on);
    // The push connection.
    void startPush();
    void enterIdle();
    void pushFailed();
    void pushTick();

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
    bool m_ready = false;       // the folder connection was logged in (a failure now is a lost line)
    qint64 m_lastReconnect = 0; // at most one quiet reconnect in 30 s: no loop with a server that keeps dropping us

    struct Read
    {
        QByteArray path;
        qint64 uid = 0;
        BodyDone done;
        bool retried = false; // once more on a new connection, then the error
    };
    ImapClient m_reader;
    QList<Read> m_reads;
    bool m_reading = false;
    bool m_readerOpening = false;
    QByteArray m_readerSelected;

    ImapClient m_push;
    bool m_pushOn = false;
    bool m_pushOpening = false;
    bool m_pushMissed = false;  // it was down: the inbox is read again once it is back
    QTimer m_pushRetry;          // connecting again, later
    QTimer m_pushCheck;          // a few changes together: one check
    QTimer m_pushWatch;          // once a minute: renew, or notice a sleep
    qint64 m_idleSince = 0;      // wall clock
    qint64 m_lastTick = 0;       // wall clock, of the watch's last tick
    int m_pushRenewMs = 9 * 60 * 1000; // servers drop an IDLE after 30 minutes; NATs sooner
    int m_pushRetryFirstMs = 30 * 1000;
    int m_pushRetryMs = 30 * 1000;

    int m_prefetch = Prefetch;
    int m_probeAfterMs = 90 * 1000;
    int m_probeTimeoutMs = 10 * 1000;
};
