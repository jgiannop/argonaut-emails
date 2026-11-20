#pragma once

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>

// The mail kept on this computer: folders, the summaries the lists are
// made of, and the messages that were opened (so they open again without
// the network). One SQLite file, $XDG_DATA_HOME/argonaut-emails/mail.sqlite.
//
// IMAP's rule for what may be kept: a folder's UIDs are only valid with
// its UIDVALIDITY; when that changes, what was kept of it is dropped.
class MailStore : public QObject
{
    Q_OBJECT

public:
    struct Folder
    {
        QString account;
        QByteArray path; // as the server names it (modified UTF-7)
        QString name;    // for people: the last part, decoded
        QString role;    // "inbox", "sent", "drafts", "trash", "junk", "archive" or empty
        char delimiter = '/';
        bool selectable = true;
        qint64 uidValidity = 0;
        int unread = 0;
        int total = 0;
    };

    struct Summary
    {
        QString account;
        QByteArray folder;
        qint64 uid = 0;
        QString subject;
        QString fromName;
        QString fromEmail;
        QString to;
        QDateTime date;
        QByteArray messageId;
        qint64 size = 0;
        bool seen = false;
        bool flagged = false;
        bool answered = false;
        bool attachments = false; // a guess from the Content-Type: multipart/mixed
    };

    // `file` empty: the default place.
    explicit MailStore(const QString &file = QString(), QObject *parent = nullptr);
    ~MailStore() override;
    bool isOpen() const { return m_db.isOpen(); }
    QString error() const { return m_error; }

    // Folders: replaces the account's list, keeping what was kept of
    // folders that are still there.
    void setFolders(const QString &account, const QList<Folder> &folders);
    QList<Folder> folders(const QString &account) const;
    Folder folder(const QString &account, const QByteArray &path) const;
    // A new UIDVALIDITY: every message kept of the folder is dropped.
    void setUidValidity(const QString &account, const QByteArray &path, qint64 uidValidity);
    qint64 highestUid(const QString &account, const QByteArray &path) const;
    qint64 lowestUid(const QString &account, const QByteArray &path) const;

    void addSummaries(const QList<Summary> &summaries);
    QList<Summary> messages(const QString &account, const QByteArray &path, int limit = 5000) const;
    Summary summary(const QString &account, const QByteArray &path, qint64 uid) const;
    void setFlags(const QString &account, const QByteArray &path, qint64 uid, bool seen, bool flagged, bool answered);
    // After a look at the server: the UIDs of [from, to] that aren't in `present` are gone.
    void removeMissing(const QString &account, const QByteArray &path, qint64 from, qint64 to, const QSet<qint64> &present);
    void removeMessage(const QString &account, const QByteArray &path, qint64 uid);

    void setBody(const QString &account, const QByteArray &path, qint64 uid, const QByteArray &raw);
    QByteArray body(const QString &account, const QByteArray &path, qint64 uid) const;

    void removeAccount(const QString &account);

    static QString defaultFile();

signals:
    // Something of that folder changed: lists show it again.
    void folderChanged(const QString &account, const QByteArray &path);
    void foldersChanged(const QString &account);

private:
    void createTables();
    void countFolder(const QString &account, const QByteArray &path);

    QSqlDatabase m_db;
    QString m_connection;
    QString m_error;
};
