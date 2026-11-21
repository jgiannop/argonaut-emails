#include "MailStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

MailStore::MailStore(const QString &file, QObject *parent)
    : QObject(parent)
{
    const QString path = file.isEmpty() ? defaultFile() : file;
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Only you can read your mail.
    QFile::setPermissions(QFileInfo(path).absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    m_connection = QStringLiteral("mailstore-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return;
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    createTables();
}

MailStore::~MailStore()
{
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}

QString MailStore::defaultFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/mail.sqlite");
}

void MailStore::createTables()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS folders ("
                          " account TEXT NOT NULL, path BLOB NOT NULL, name TEXT, role TEXT, delimiter TEXT,"
                          " selectable INTEGER, uidvalidity INTEGER DEFAULT 0, unread INTEGER DEFAULT 0,"
                          " total INTEGER DEFAULT 0, position INTEGER DEFAULT 0,"
                          " PRIMARY KEY (account, path))"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS messages ("
                          " account TEXT NOT NULL, folder BLOB NOT NULL, uid INTEGER NOT NULL,"
                          " subject TEXT, from_name TEXT, from_email TEXT, to_text TEXT, date INTEGER,"
                          " message_id BLOB, size INTEGER, seen INTEGER, flagged INTEGER, answered INTEGER,"
                          " attachments INTEGER, body BLOB,"
                          " PRIMARY KEY (account, folder, uid))"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS messages_by_date ON messages (account, folder, date)"));
    // Added in 0.2 (Gmail's inbox tabs); a store made before gets it here.
    bool hasCategory = false;
    q.exec(QStringLiteral("PRAGMA table_info(messages)"));
    while (q.next())
        hasCategory = hasCategory || q.value(1).toString() == QLatin1String("category");
    if (!hasCategory)
        q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN category TEXT NOT NULL DEFAULT ''"));
    // The column holds a list, ",primary,updates,"; the first builds kept one tab, "updates": made a list.
    q.exec(QStringLiteral("UPDATE messages SET category = ',' || category || ',' WHERE category <> '' AND category NOT LIKE ',%'"));
}

void MailStore::setFolders(const QString &account, const QList<Folder> &folders)
{
    m_db.transaction();
    QSet<QByteArray> keep;
    int position = 0;
    for (const Folder &f : folders) {
        keep.insert(f.path);
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("INSERT INTO folders (account, path, name, role, delimiter, selectable, position)"
                                 " VALUES (?, ?, ?, ?, ?, ?, ?)"
                                 " ON CONFLICT (account, path) DO UPDATE SET name = excluded.name, role = excluded.role,"
                                 " delimiter = excluded.delimiter, selectable = excluded.selectable, position = excluded.position"));
        q.addBindValue(account);
        q.addBindValue(f.path);
        q.addBindValue(f.name);
        q.addBindValue(f.role);
        q.addBindValue(QString(QLatin1Char(f.delimiter ? f.delimiter : '/')));
        q.addBindValue(f.selectable ? 1 : 0);
        q.addBindValue(position++);
        q.exec();
    }
    // Folders that are gone, and their messages.
    QSqlQuery list(m_db);
    list.prepare(QStringLiteral("SELECT path FROM folders WHERE account = ?"));
    list.addBindValue(account);
    list.exec();
    QList<QByteArray> gone;
    while (list.next()) {
        const QByteArray path = list.value(0).toByteArray();
        if (!keep.contains(path))
            gone.append(path);
    }
    for (const QByteArray &path : std::as_const(gone)) {
        for (const char *sql : {"DELETE FROM folders WHERE account = ? AND path = ?", "DELETE FROM messages WHERE account = ? AND folder = ?"}) {
            QSqlQuery d(m_db);
            d.prepare(QString::fromLatin1(sql));
            d.addBindValue(account);
            d.addBindValue(path);
            d.exec();
        }
    }
    m_db.commit();
    emit foldersChanged(account);
}

QList<MailStore::Folder> MailStore::folders(const QString &account) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT path, name, role, delimiter, selectable, uidvalidity, unread, total FROM folders"
                             " WHERE account = ? ORDER BY position"));
    q.addBindValue(account);
    q.exec();
    QList<Folder> out;
    while (q.next()) {
        Folder f;
        f.account = account;
        f.path = q.value(0).toByteArray();
        f.name = q.value(1).toString();
        f.role = q.value(2).toString();
        const QString d = q.value(3).toString();
        f.delimiter = d.isEmpty() ? '/' : d.at(0).toLatin1();
        f.selectable = q.value(4).toInt() != 0;
        f.uidValidity = q.value(5).toLongLong();
        f.unread = q.value(6).toInt();
        f.total = q.value(7).toInt();
        out.append(f);
    }
    return out;
}

MailStore::Folder MailStore::folder(const QString &account, const QByteArray &path) const
{
    for (const Folder &f : folders(account)) {
        if (f.path == path)
            return f;
    }
    return {};
}

void MailStore::setUidValidity(const QString &account, const QByteArray &path, qint64 uidValidity)
{
    const Folder f = folder(account, path);
    if (f.uidValidity == uidValidity)
        return;
    m_db.transaction();
    if (f.uidValidity != 0) { // the UIDs we have mean nothing now
        QSqlQuery d(m_db);
        d.prepare(QStringLiteral("DELETE FROM messages WHERE account = ? AND folder = ?"));
        d.addBindValue(account);
        d.addBindValue(path);
        d.exec();
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE folders SET uidvalidity = ? WHERE account = ? AND path = ?"));
    q.addBindValue(uidValidity);
    q.addBindValue(account);
    q.addBindValue(path);
    q.exec();
    m_db.commit();
    countFolder(account, path);
    emit folderChanged(account, path);
}

qint64 MailStore::highestUid(const QString &account, const QByteArray &path) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT MAX(uid) FROM messages WHERE account = ? AND folder = ?"));
    q.addBindValue(account);
    q.addBindValue(path);
    q.exec();
    return q.next() ? q.value(0).toLongLong() : 0;
}

qint64 MailStore::lowestUid(const QString &account, const QByteArray &path) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT MIN(uid) FROM messages WHERE account = ? AND folder = ?"));
    q.addBindValue(account);
    q.addBindValue(path);
    q.exec();
    return q.next() ? q.value(0).toLongLong() : 0;
}

void MailStore::addSummaries(const QList<Summary> &summaries)
{
    if (summaries.isEmpty())
        return;
    m_db.transaction();
    QSet<QPair<QString, QByteArray>> touched;
    for (const Summary &s : summaries) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("INSERT INTO messages (account, folder, uid, subject, from_name, from_email, to_text, date,"
                                 " message_id, size, seen, flagged, answered, attachments) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
                                 " ON CONFLICT (account, folder, uid) DO UPDATE SET seen = excluded.seen,"
                                 " flagged = excluded.flagged, answered = excluded.answered"));
        q.addBindValue(s.account);
        q.addBindValue(s.folder);
        q.addBindValue(s.uid);
        q.addBindValue(s.subject);
        q.addBindValue(s.fromName);
        q.addBindValue(s.fromEmail);
        q.addBindValue(s.to);
        q.addBindValue(s.date.isValid() ? s.date.toSecsSinceEpoch() : 0);
        q.addBindValue(s.messageId);
        q.addBindValue(s.size);
        q.addBindValue(s.seen ? 1 : 0);
        q.addBindValue(s.flagged ? 1 : 0);
        q.addBindValue(s.answered ? 1 : 0);
        q.addBindValue(s.attachments ? 1 : 0);
        q.exec();
        touched.insert({s.account, s.folder});
    }
    m_db.commit();
    for (const auto &t : std::as_const(touched)) {
        countFolder(t.first, t.second);
        emit folderChanged(t.first, t.second);
    }
}

namespace {

MailStore::Summary summaryFrom(const QSqlQuery &q)
{
    MailStore::Summary s;
    s.account = q.value(0).toString();
    s.folder = q.value(1).toByteArray();
    s.uid = q.value(2).toLongLong();
    s.subject = q.value(3).toString();
    s.fromName = q.value(4).toString();
    s.fromEmail = q.value(5).toString();
    s.to = q.value(6).toString();
    const qint64 date = q.value(7).toLongLong();
    if (date > 0)
        s.date = QDateTime::fromSecsSinceEpoch(date);
    s.messageId = q.value(8).toByteArray();
    s.size = q.value(9).toLongLong();
    s.seen = q.value(10).toInt() != 0;
    s.flagged = q.value(11).toInt() != 0;
    s.answered = q.value(12).toInt() != 0;
    s.attachments = q.value(13).toInt() != 0;
    s.categories = q.value(14).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    return s;
}

const char *kSummaryColumns = "account, folder, uid, subject, from_name, from_email, to_text, date, message_id, size,"
                              " seen, flagged, answered, attachments, category";

} // namespace

QStringList MailStore::categories()
{
    return {QStringLiteral("primary"), QStringLiteral("promotions"), QStringLiteral("social"), QStringLiteral("updates"),
            QStringLiteral("forums")};
}

namespace {

// The WHERE part that picks one tab's messages, and its value.
QString categoryClause(const QString &category)
{
    if (category == QLatin1String("primary"))
        return QStringLiteral(" AND (category = '' OR category LIKE '%,primary,%')");
    return QStringLiteral(" AND category LIKE ?");
}

QString categoryPattern(const QString &category)
{
    return QStringLiteral("%,") + category + QStringLiteral(",%");
}

} // namespace

QList<MailStore::Summary> MailStore::messages(const QString &account, const QByteArray &path, int limit, const QString &category) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM messages WHERE account = ? AND folder = ?%2 ORDER BY date DESC, uid DESC LIMIT ?")
                  .arg(QLatin1String(kSummaryColumns), category.isEmpty() ? QString() : categoryClause(category)));
    q.addBindValue(account);
    q.addBindValue(path);
    if (!category.isEmpty() && category != QLatin1String("primary"))
        q.addBindValue(categoryPattern(category));
    q.addBindValue(limit);
    q.exec();
    QList<Summary> out;
    while (q.next())
        out.append(summaryFrom(q));
    return out;
}

void MailStore::setCategories(const QString &account, const QByteArray &path, const QHash<qint64, QStringList> &categories)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT uid, category FROM messages WHERE account = ? AND folder = ?"));
    q.addBindValue(account);
    q.addBindValue(path);
    q.exec();
    QList<QPair<qint64, QString>> changes;
    while (q.next()) {
        const qint64 uid = q.value(0).toLongLong();
        QStringList tabs = categories.value(uid);
        tabs.sort();
        const QString now = tabs.isEmpty() ? QString(QLatin1String("")) : QLatin1Char(',') + tabs.join(QLatin1Char(',')) + QLatin1Char(',');
        if (q.value(1).toString() != now)
            changes.append({uid, now});
    }
    if (changes.isEmpty())
        return; // as it was: the list isn't made again
    m_db.transaction();
    for (const auto &c : std::as_const(changes)) {
        QSqlQuery u(m_db);
        u.prepare(QStringLiteral("UPDATE messages SET category = ? WHERE account = ? AND folder = ? AND uid = ?"));
        u.addBindValue(c.second);
        u.addBindValue(account);
        u.addBindValue(path);
        u.addBindValue(c.first);
        u.exec();
    }
    m_db.commit();
    emit folderChanged(account, path);
}

QHash<QString, QPair<int, int>> MailStore::categoryCounts(const QString &account, const QByteArray &path) const
{
    {
        QSqlQuery any(m_db);
        any.prepare(QStringLiteral("SELECT 1 FROM messages WHERE account = ? AND folder = ? AND category <> '' LIMIT 1"));
        any.addBindValue(account);
        any.addBindValue(path);
        any.exec();
        if (!any.next())
            return {}; // not Gmail's inbox, or not known yet
    }
    QHash<QString, QPair<int, int>> out;
    for (const QString &category : categories()) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT count(*), coalesce(sum(seen = 0), 0) FROM messages WHERE account = ? AND folder = ?")
                  + categoryClause(category));
        q.addBindValue(account);
        q.addBindValue(path);
        if (category != QLatin1String("primary"))
            q.addBindValue(categoryPattern(category));
        q.exec();
        if (q.next())
            out.insert(category, {q.value(0).toInt(), q.value(1).toInt()});
    }
    return out;
}

MailStore::Summary MailStore::summary(const QString &account, const QByteArray &path, qint64 uid) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM messages WHERE account = ? AND folder = ? AND uid = ?").arg(QLatin1String(kSummaryColumns)));
    q.addBindValue(account);
    q.addBindValue(path);
    q.addBindValue(uid);
    q.exec();
    return q.next() ? summaryFrom(q) : Summary();
}

void MailStore::setFlags(const QString &account, const QByteArray &path, qint64 uid, bool seen, bool flagged, bool answered)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE messages SET seen = ?, flagged = ?, answered = ? WHERE account = ? AND folder = ? AND uid = ?"));
    q.addBindValue(seen ? 1 : 0);
    q.addBindValue(flagged ? 1 : 0);
    q.addBindValue(answered ? 1 : 0);
    q.addBindValue(account);
    q.addBindValue(path);
    q.addBindValue(uid);
    q.exec();
    countFolder(account, path);
    emit folderChanged(account, path);
}

void MailStore::removeMissing(const QString &account, const QByteArray &path, qint64 from, qint64 to, const QSet<qint64> &present)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT uid FROM messages WHERE account = ? AND folder = ? AND uid BETWEEN ? AND ?"));
    q.addBindValue(account);
    q.addBindValue(path);
    q.addBindValue(from);
    q.addBindValue(to);
    q.exec();
    QList<qint64> gone;
    while (q.next()) {
        const qint64 uid = q.value(0).toLongLong();
        if (!present.contains(uid))
            gone.append(uid);
    }
    if (gone.isEmpty())
        return;
    m_db.transaction();
    for (const qint64 uid : std::as_const(gone)) {
        QSqlQuery d(m_db);
        d.prepare(QStringLiteral("DELETE FROM messages WHERE account = ? AND folder = ? AND uid = ?"));
        d.addBindValue(account);
        d.addBindValue(path);
        d.addBindValue(uid);
        d.exec();
    }
    m_db.commit();
    countFolder(account, path);
    emit folderChanged(account, path);
}

void MailStore::removeMessage(const QString &account, const QByteArray &path, qint64 uid)
{
    QSqlQuery d(m_db);
    d.prepare(QStringLiteral("DELETE FROM messages WHERE account = ? AND folder = ? AND uid = ?"));
    d.addBindValue(account);
    d.addBindValue(path);
    d.addBindValue(uid);
    d.exec();
    countFolder(account, path);
    emit folderChanged(account, path);
}

void MailStore::setBody(const QString &account, const QByteArray &path, qint64 uid, const QByteArray &raw)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE messages SET body = ? WHERE account = ? AND folder = ? AND uid = ?"));
    q.addBindValue(raw);
    q.addBindValue(account);
    q.addBindValue(path);
    q.addBindValue(uid);
    q.exec();
}

QByteArray MailStore::body(const QString &account, const QByteArray &path, qint64 uid) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT body FROM messages WHERE account = ? AND folder = ? AND uid = ?"));
    q.addBindValue(account);
    q.addBindValue(path);
    q.addBindValue(uid);
    q.exec();
    return q.next() ? q.value(0).toByteArray() : QByteArray();
}

QList<qint64> MailStore::withoutBody(const QString &account, const QByteArray &path, int limit, qint64 maxSize) const
{
    QList<qint64> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT uid FROM messages WHERE account = ? AND folder = ? AND body IS NULL AND size <= ?"
                             " ORDER BY date DESC, uid DESC LIMIT ?"));
    q.addBindValue(account);
    q.addBindValue(path);
    q.addBindValue(maxSize);
    q.addBindValue(limit);
    q.exec();
    while (q.next())
        out.append(q.value(0).toLongLong());
    return out;
}

void MailStore::removeAccount(const QString &account)
{
    for (const char *sql : {"DELETE FROM messages WHERE account = ?", "DELETE FROM folders WHERE account = ?"}) {
        QSqlQuery d(m_db);
        d.prepare(QString::fromLatin1(sql));
        d.addBindValue(account);
        d.exec();
    }
    emit foldersChanged(account);
}

void MailStore::countFolder(const QString &account, const QByteArray &path)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE folders SET"
                             " unread = (SELECT COUNT(*) FROM messages WHERE account = ? AND folder = ? AND seen = 0),"
                             " total = (SELECT COUNT(*) FROM messages WHERE account = ? AND folder = ?)"
                             " WHERE account = ? AND path = ?"));
    for (int i = 0; i < 3; ++i) {
        q.addBindValue(account);
        q.addBindValue(path);
    }
    q.exec();
}
