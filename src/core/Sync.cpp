#include "Sync.h"

#include "Mime.h"

#include <algorithm>
#include <memory>

namespace {

const QByteArray kHeaderFields = "BODY.PEEK[HEADER.FIELDS (FROM TO CC SUBJECT DATE MESSAGE-ID CONTENT-TYPE)]";

// The value of the first FETCH item whose name starts with `prefix`.
const Imap::Value *itemStartingWith(const Imap::Value &list, const char *prefix)
{
    for (qsizetype i = 0; i + 1 < list.items.size(); i += 2) {
        const Imap::Value &name = list.items.at(i);
        if (name.kind == Imap::Value::Atom && name.text.toUpper().startsWith(prefix))
            return &list.items.at(i + 1);
    }
    return nullptr;
}

// "* 12 FETCH (...)": the list, else null.
const Imap::Value *fetchList(const Imap::Response &r)
{
    if (r.values.size() >= 3 && r.values.at(1).is("FETCH") && r.values.at(2).isList())
        return &r.values.at(2);
    return nullptr;
}

bool hasFlag(const Imap::Value *flags, const char *flag)
{
    if (!flags)
        return false;
    for (const Imap::Value &f : flags->items) {
        if (f.text.compare(flag, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

// What the server says a folder is for (RFC 6154 SPECIAL-USE), else empty.
QString roleFromFlags(const Imap::Value &flags)
{
    for (const Imap::Value &f : flags.items) {
        const QByteArray flag = f.text.toLower();
        if (flag == "\\sent")
            return QStringLiteral("sent");
        if (flag == "\\drafts")
            return QStringLiteral("drafts");
        if (flag == "\\trash")
            return QStringLiteral("trash");
        if (flag == "\\junk")
            return QStringLiteral("junk");
        if (flag == "\\archive")
            return QStringLiteral("archive");
        if (flag == "\\all")
            return QStringLiteral("all");
        if (flag == "\\flagged")
            return QStringLiteral("starred");
    }
    return {};
}

// Servers without SPECIAL-USE: the usual names. Only for them: on Gmail a label named
// "Deleted Items" (an old Outlook's) isn't the Trash.
QString roleFromName(const QString &path)
{
    const QString last = path.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), -1).toLower();
    if (last == QLatin1String("sent") || last == QLatin1String("sent items") || last == QLatin1String("sent messages") || last == QLatin1String("sent mail"))
        return QStringLiteral("sent");
    if (last == QLatin1String("drafts"))
        return QStringLiteral("drafts");
    if (last == QLatin1String("trash") || last == QLatin1String("deleted items") || last == QLatin1String("deleted messages"))
        return QStringLiteral("trash");
    if (last == QLatin1String("junk") || last == QLatin1String("spam"))
        return QStringLiteral("junk");
    if (last == QLatin1String("archive"))
        return QStringLiteral("archive");
    return {};
}

// "17-Jul-1996 02:44:25 -0700"
QDateTime internalDate(const QString &text)
{
    const QStringList parts = text.simplified().split(QLatin1Char(' '));
    if (parts.size() != 3)
        return {};
    QDateTime date = QDateTime::fromString(parts.at(0) + QLatin1Char(' ') + parts.at(1), QStringLiteral("d-MMM-yyyy HH:mm:ss"));
    const QString zone = parts.at(2);
    if (!date.isValid() || zone.size() != 5)
        return {};
    const int offset = (zone.mid(1, 2).toInt() * 3600 + zone.mid(3, 2).toInt() * 60) * (zone.startsWith(QLatin1Char('-')) ? -1 : 1);
    date.setOffsetFromUtc(offset);
    return date;
}

int roleOrder(const QString &role)
{
    static const QStringList order{QStringLiteral("inbox"),   QStringLiteral("all"),  QStringLiteral("starred"),
                                   QStringLiteral("sent"),    QStringLiteral("drafts"), QStringLiteral("archive"),
                                   QStringLiteral("junk"),    QStringLiteral("trash")};
    const qsizetype i = order.indexOf(role);
    return i < 0 ? int(order.size()) : int(i);
}

} // namespace

Sync::Sync(const Account &account, MailStore *store, QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_store(store)
{
    connect(&m_client, &ImapClient::ready, this, [this] {
        m_opening = false;
        runNext();
    });
    connect(&m_client, &ImapClient::failed, this, &Sync::connectFailed);
    m_poll.setInterval(5 * 60 * 1000);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (!isBusy())
            syncAll();
    });
}

void Sync::setPollInterval(int ms)
{
    m_poll.setInterval(ms);
}

void Sync::start()
{
    enqueue({tr("Looking for folders"), [this](auto next) { listFolders(next); }, {}});
    const QByteArray inbox = "INBOX";
    syncFolder(inbox);
    m_poll.start();
}

void Sync::syncAll()
{
    enqueue({tr("Looking for folders"), [this](auto next) { listFolders(next); }, {}});
    QList<QByteArray> folders(m_looked.begin(), m_looked.end());
    if (!folders.contains("INBOX"))
        folders.prepend("INBOX");
    for (const QByteArray &f : std::as_const(folders))
        syncFolder(f);
}

void Sync::syncFolder(const QByteArray &path)
{
    // The same folder already waiting: once is enough.
    for (const Op &op : std::as_const(m_ops)) {
        if (op.what == tr("Checking %1").arg(Imap::decodeMailboxName(path)))
            return;
    }
    m_looked.insert(path);
    enqueue({tr("Checking %1").arg(Imap::decodeMailboxName(path)), [this, path](auto next) { readFolder(path, next); }, {}});
}

void Sync::enqueue(Op op)
{
    if (op.urgent) {
        // After the urgent ones already waiting, before everything else.
        qsizetype at = 0;
        while (at < m_ops.size() && m_ops.at(at).urgent)
            ++at;
        m_ops.insert(at, std::move(op));
    } else {
        m_ops.append(std::move(op));
    }
    runNext();
}

void Sync::runNext()
{
    if (m_running)
        return;
    if (m_ops.isEmpty()) {
        emit status(QString());
        emit idle();
        return;
    }
    if (!m_client.isReady()) {
        if (!m_opening) {
            m_opening = true;
            m_selected.clear();
            emit status(tr("Connecting to %1").arg(m_account.imapHost));
            m_client.open(m_haveSettings ? m_settings : m_account.imapSettings());
        }
        return;
    }
    m_running = true;
    Op op = m_ops.takeFirst();
    emit status(op.what + QStringLiteral("…"));
    op.run([this] {
        m_running = false;
        runNext();
    });
}

void Sync::connectFailed(const QString &message)
{
    m_opening = false;
    m_running = false;
    m_selected.clear();
    const QList<Op> dropped = m_ops;
    m_ops.clear();
    for (const Op &op : dropped) {
        if (op.failed)
            op.failed(message);
    }
    emit status(QString());
    emit error(tr("%1: %2").arg(m_account.title(), message));
    emit idle();
}

void Sync::select(const QByteArray &path, std::function<void(bool ok, const ImapClient::Result &result)> done)
{
    bool ok = false;
    const QByteArray name = Imap::quoted(path, &ok);
    if (!ok) {
        done(false, {});
        return;
    }
    m_client.command("SELECT " + name, [this, path, done](const ImapClient::Result &r) {
        m_selected = r.ok ? path : QByteArray();
        done(r.ok, r);
    });
}

void Sync::listFolders(std::function<void()> next)
{
    m_client.command("LIST \"\" \"*\"", [this, next](const ImapClient::Result &r) {
        if (!r.ok) {
            emit error(tr("The folders couldn't be listed: %1").arg(r.text));
            next();
            return;
        }
        QList<MailStore::Folder> folders;
        bool flagged = false; // the server marks its special folders: trust only that
        for (const Imap::Response &u : r.untagged) {
            if (u.values.size() < 4 || !u.values.at(0).is("LIST") || !u.values.at(1).isList())
                continue;
            MailStore::Folder f;
            f.account = m_account.id;
            const Imap::Value &flags = u.values.at(1);
            const QByteArray delimiter = u.values.at(2).bytes();
            f.delimiter = delimiter.isEmpty() ? '/' : delimiter.at(0);
            f.path = u.values.at(3).bytes();
            if (f.path.isEmpty())
                continue;
            const QString decoded = Imap::decodeMailboxName(f.path);
            f.name = decoded.section(QLatin1Char(f.delimiter), -1);
            if (f.path.compare("INBOX", Qt::CaseInsensitive) == 0) {
                f.path = "INBOX";
                f.name = tr("Inbox");
            }
            f.selectable = !hasFlag(&flags, "\\Noselect") && !hasFlag(&flags, "\\NonExistent");
            f.role = f.path == "INBOX" ? QStringLiteral("inbox") : roleFromFlags(flags);
            if (f.role == QLatin1String("all"))
                f.name = tr("All Mail"); // Gmail names it in the account's language
            flagged = flagged || !roleFromFlags(flags).isEmpty();
            folders.append(f);
        }
        if (!flagged) {
            for (MailStore::Folder &f : folders) {
                if (f.role.isEmpty())
                    f.role = roleFromName(Imap::decodeMailboxName(f.path));
            }
        }
        // The usual ones first, the rest as the server has them by name.
        std::stable_sort(folders.begin(), folders.end(), [](const MailStore::Folder &a, const MailStore::Folder &b) {
            // The special folders go next to the Inbox, even from inside "[Gmail]".
            const bool aTop = !a.path.contains(a.delimiter) || !a.role.isEmpty();
            const bool bTop = !b.path.contains(b.delimiter) || !b.role.isEmpty();
            const int ra = aTop ? roleOrder(a.role) : 99;
            const int rb = bTop ? roleOrder(b.role) : 99;
            if (ra != rb)
                return ra < rb;
            return QString::compare(Imap::decodeMailboxName(a.path), Imap::decodeMailboxName(b.path), Qt::CaseInsensitive) < 0;
        });
        m_store->setFolders(m_account.id, folders);
        next();
    });
}

void Sync::readFolder(const QByteArray &path, std::function<void()> then)
{
    select(path, [this, path, then](bool ok, const ImapClient::Result &r) {
        if (!ok) {
            if (!r.text.isEmpty())
                emit error(tr("%1 couldn't be opened: %2").arg(Imap::decodeMailboxName(path), r.text));
            then();
            return;
        }
        // Gmail's inbox: its tabs after its messages.
        const bool tabs = path == "INBOX" && m_client.hasCapability("X-GM-EXT-1");
        const std::function<void()> next = !tabs ? then : [this, path, then] { readCategories(path, then); };
        qint64 exists = 0;
        qint64 uidValidity = 0;
        for (const Imap::Response &u : r.untagged) {
            if (u.values.size() >= 2 && u.values.at(1).is("EXISTS"))
                exists = u.values.at(0).text.toLongLong();
            if (u.code.toUpper().startsWith("UIDVALIDITY "))
                uidValidity = u.code.mid(12).trimmed().toLongLong();
        }
        if (uidValidity != 0)
            m_store->setUidValidity(m_account.id, path, uidValidity);
        if (exists == 0) {
            m_store->removeMissing(m_account.id, path, 0, std::numeric_limits<qint64>::max(), {});
            next();
            return;
        }
        const qint64 highest = m_store->highestUid(m_account.id, path);
        const QByteArray items = "(UID FLAGS RFC822.SIZE INTERNALDATE " + kHeaderFields + ')';
        const QByteArray fetch = highest == 0
            ? "FETCH " + QByteArray::number(qMax<qint64>(1, exists - FirstFetch + 1)) + ":* " + items
            : "UID FETCH " + QByteArray::number(highest + 1) + ":* " + items;
        m_client.command(fetch, [this, path, highest, next](const ImapClient::Result &f) {
            QList<MailStore::Summary> summaries;
            for (const Imap::Response &u : f.untagged) {
                const Imap::Value *list = fetchList(u);
                if (!list)
                    continue;
                const Imap::Value *uid = Imap::fetchItem(*list, "UID");
                if (!uid || uid->text.toLongLong() <= highest)
                    continue; // "n:*" gives the last one even when nothing is newer
                MailStore::Summary s;
                s.account = m_account.id;
                s.folder = path;
                s.uid = uid->text.toLongLong();
                const Imap::Value *flags = Imap::fetchItem(*list, "FLAGS");
                s.seen = hasFlag(flags, "\\Seen");
                s.flagged = hasFlag(flags, "\\Flagged");
                s.answered = hasFlag(flags, "\\Answered");
                if (const Imap::Value *size = Imap::fetchItem(*list, "RFC822.SIZE"))
                    s.size = size->text.toLongLong();
                if (const Imap::Value *headers = itemStartingWith(*list, "BODY[")) {
                    Mime::Message m;
                    m.root = Mime::parseHeaders(headers->text);
                    s.subject = m.subject();
                    const QList<Mime::Address> from = m.from();
                    if (!from.isEmpty()) {
                        s.fromName = from.first().name;
                        s.fromEmail = from.first().email;
                    }
                    QStringList to;
                    for (const Mime::Address &a : m.to())
                        to.append(a.name.isEmpty() ? a.email : a.name);
                    s.to = to.join(QStringLiteral(", "));
                    s.date = m.date();
                    s.messageId = m.messageId();
                    s.attachments = m.root.type == "multipart/mixed";
                }
                if (!s.date.isValid()) {
                    if (const Imap::Value *internal = Imap::fetchItem(*list, "INTERNALDATE"))
                        s.date = internalDate(internal->string());
                }
                summaries.append(s);
            }
            m_store->addSummaries(summaries);
            // The flags of what was kept before: read elsewhere, or gone.
            const qint64 low = m_store->lowestUid(m_account.id, path);
            if (highest == 0 || low == 0) {
                next();
                return;
            }
            m_client.command("UID FETCH " + QByteArray::number(low) + ':' + QByteArray::number(highest) + " (UID FLAGS)",
                             [this, path, low, highest, next](const ImapClient::Result &g) {
                                 if (g.ok) {
                                     QSet<qint64> present;
                                     for (const Imap::Response &u : g.untagged) {
                                         const Imap::Value *list = fetchList(u);
                                         const Imap::Value *uid = list ? Imap::fetchItem(*list, "UID") : nullptr;
                                         if (!uid)
                                             continue;
                                         const qint64 n = uid->text.toLongLong();
                                         present.insert(n);
                                         const Imap::Value *flags = Imap::fetchItem(*list, "FLAGS");
                                         const MailStore::Summary s = m_store->summary(m_account.id, path, n);
                                         const bool seen = hasFlag(flags, "\\Seen");
                                         const bool flagged = hasFlag(flags, "\\Flagged");
                                         const bool answered = hasFlag(flags, "\\Answered");
                                         if (s.uid != 0 && (s.seen != seen || s.flagged != flagged || s.answered != answered))
                                             m_store->setFlags(m_account.id, path, n, seen, flagged, answered);
                                     }
                                     m_store->removeMissing(m_account.id, path, low, highest, present);
                                 }
                                 next();
                             });
        });
    });
}

void Sync::readCategories(const QByteArray &path, std::function<void()> next)
{
    readCategory(path, 0, std::make_shared<QHash<qint64, QStringList>>(), next);
}

// One search per tab, one after the other; a failed one leaves what was known.
// Primary too: Gmail shows some messages in two tabs (an important one of Updates also in Primary).
void Sync::readCategory(const QByteArray &path, int i, std::shared_ptr<QHash<qint64, QStringList>> found, std::function<void()> next)
{
    const QStringList categories = MailStore::categories();
    if (i >= categories.size()) {
        m_store->setCategories(m_account.id, path, *found);
        next();
        return;
    }
    const QString category = categories.at(i);
    m_client.command("UID SEARCH X-GM-RAW \"category:" + category.toLatin1() + '"',
                     [this, path, i, found, next, category](const ImapClient::Result &r) {
                         if (!r.ok) {
                             next();
                             return;
                         }
                         for (const Imap::Response &u : r.untagged) {
                             if (u.values.isEmpty() || !u.values.at(0).is("SEARCH"))
                                 continue;
                             for (qsizetype k = 1; k < u.values.size(); ++k) {
                                 const qint64 uid = u.values.at(k).text.toLongLong();
                                 if (uid > 0)
                                     (*found)[uid].append(category);
                             }
                         }
                         readCategory(path, i + 1, found, next);
                     });
}

void Sync::fetchBody(const QByteArray &path, qint64 uid, BodyDone done)
{
    const QByteArray kept = m_store->body(m_account.id, path, uid);
    if (!kept.isEmpty()) {
        done(kept, QString());
        return;
    }
    Op op;
    op.what = tr("Opening the message");
    op.urgent = true; // the user is waiting for it
    op.failed = [done](const QString &e) { done({}, e); };
    op.run = [this, path, uid, done](std::function<void()> next) {
        auto fetch = [this, path, uid, done, next] {
            m_client.command("UID FETCH " + QByteArray::number(uid) + " (UID BODY.PEEK[])", [this, path, uid, done, next](const ImapClient::Result &r) {
                QByteArray raw;
                for (const Imap::Response &u : r.untagged) {
                    const Imap::Value *list = fetchList(u);
                    const Imap::Value *id = list ? Imap::fetchItem(*list, "UID") : nullptr;
                    if (!id || id->text.toLongLong() != uid)
                        continue;
                    if (const Imap::Value *body = itemStartingWith(*list, "BODY["))
                        raw = body->text;
                }
                if (raw.isEmpty()) {
                    done({}, r.ok ? tr("The message is no longer on the server") : r.text);
                } else {
                    m_store->setBody(m_account.id, path, uid, raw);
                    done(raw, QString());
                }
                next();
            });
        };
        if (m_selected == path) {
            fetch();
            return;
        }
        select(path, [done, fetch, next](bool ok, const ImapClient::Result &r) {
            if (!ok) {
                done({}, r.text);
                next();
                return;
            }
            fetch();
        });
    };
    enqueue(op);
}

void Sync::fetchBodies(const QByteArray &path, const QList<qint64> &uids, std::function<void(const QString &error)> done)
{
    auto left = std::make_shared<QList<qint64>>();
    for (qint64 uid : uids) {
        if (m_store->body(m_account.id, path, uid).isEmpty())
            left->append(uid);
    }
    if (left->isEmpty()) {
        done(QString());
        return;
    }
    Op op;
    op.what = tr("Reading %n message(s)", nullptr, int(left->size()));
    op.urgent = true; // the user chose something that waits for them
    op.failed = done;
    op.run = [this, path, left, done](std::function<void()> next) {
        auto fetch = [this, path, left, done, next] { fetchBatch(path, left, done, next); };
        if (m_selected == path) {
            fetch();
            return;
        }
        select(path, [done, fetch, next](bool ok, const ImapClient::Result &r) {
            if (!ok) {
                done(r.text);
                next();
                return;
            }
            fetch();
        });
    };
    enqueue(op);
}

// 20 at a time: one answer stays a sensible size.
void Sync::fetchBatch(const QByteArray &path, std::shared_ptr<QList<qint64>> left, std::function<void(const QString &error)> done,
                      std::function<void()> next)
{
    if (left->isEmpty()) {
        done(QString());
        next();
        return;
    }
    QByteArray set;
    const QList<qint64> batch = left->mid(0, 20);
    left->remove(0, batch.size());
    for (qint64 uid : batch)
        set += (set.isEmpty() ? QByteArray() : QByteArray(",")) + QByteArray::number(uid);
    m_client.command("UID FETCH " + set + " (UID BODY.PEEK[])", [this, path, left, done, next](const ImapClient::Result &r) {
        if (!r.ok) {
            done(r.text);
            next();
            return;
        }
        for (const Imap::Response &u : r.untagged) {
            const Imap::Value *list = fetchList(u);
            const Imap::Value *id = list ? Imap::fetchItem(*list, "UID") : nullptr;
            const Imap::Value *body = list ? itemStartingWith(*list, "BODY[") : nullptr;
            if (id && body && !body->text.isEmpty())
                m_store->setBody(m_account.id, path, id->text.toLongLong(), body->text);
        }
        fetchBatch(path, left, done, next);
    });
}

void Sync::storeFlag(const QByteArray &path, qint64 uid, const QByteArray &flag, bool on)
{
    Op op;
    op.what = tr("Saving");
    op.run = [this, path, uid, flag, on](std::function<void()> next) {
        auto store = [this, uid, flag, on, next] {
            m_client.command("UID STORE " + QByteArray::number(uid) + (on ? " +FLAGS.SILENT (" : " -FLAGS.SILENT (") + flag + ')',
                             [this, next](const ImapClient::Result &r) {
                                 if (!r.ok)
                                     emit error(tr("The server didn't save the change: %1").arg(r.text));
                                 next();
                             });
        };
        if (m_selected == path) {
            store();
            return;
        }
        select(path, [store, next](bool ok, const ImapClient::Result &) {
            if (ok)
                store();
            else
                next();
        });
    };
    enqueue(op);
}

void Sync::setSeen(const QByteArray &path, qint64 uid, bool seen)
{
    const MailStore::Summary s = m_store->summary(m_account.id, path, uid);
    if (s.uid == 0 || s.seen == seen)
        return;
    m_store->setFlags(m_account.id, path, uid, seen, s.flagged, s.answered); // at once, the server after
    storeFlag(path, uid, "\\Seen", seen);
}

void Sync::setFlagged(const QByteArray &path, qint64 uid, bool flagged)
{
    const MailStore::Summary s = m_store->summary(m_account.id, path, uid);
    if (s.uid == 0 || s.flagged == flagged)
        return;
    m_store->setFlags(m_account.id, path, uid, s.seen, flagged, s.answered);
    storeFlag(path, uid, "\\Flagged", flagged);
}

void Sync::setAnswered(const QByteArray &path, qint64 uid)
{
    const MailStore::Summary s = m_store->summary(m_account.id, path, uid);
    if (s.uid == 0 || s.answered)
        return;
    m_store->setFlags(m_account.id, path, uid, s.seen, s.flagged, true);
    storeFlag(path, uid, "\\Answered", true);
}

QByteArray Sync::folderWithRole(const QString &role) const
{
    for (const MailStore::Folder &f : m_store->folders(m_account.id)) {
        if (f.role == role && f.selectable)
            return f.path;
    }
    return {};
}

void Sync::moveToTrash(const QByteArray &path, qint64 uid)
{
    const QByteArray trash = folderWithRole(QStringLiteral("trash"));
    m_store->removeMessage(m_account.id, path, uid); // gone from the list at once
    Op op;
    op.what = tr("Moving to the Trash");
    op.run = [this, path, uid, trash](std::function<void()> next) {
        auto act = [this, uid, trash, path, next] {
            const QByteArray id = QByteArray::number(uid);
            auto done = [this, next](const ImapClient::Result &r) {
                if (!r.ok)
                    emit error(tr("The message couldn't be moved to the Trash: %1").arg(r.text));
                next();
            };
            bool ok = false;
            const QByteArray target = trash.isEmpty() ? QByteArray() : Imap::quoted(trash, &ok);
            if (trash.isEmpty() || trash == path || !ok) {
                // Already in the Trash, or there is none: marked deleted, nothing expunged
                // (EXPUNGE without UIDPLUS would take every message marked so).
                m_client.command("UID STORE " + id + " +FLAGS.SILENT (\\Deleted)", done);
                return;
            }
            if (m_client.hasCapability("MOVE")) {
                m_client.command("UID MOVE " + id + ' ' + target, done);
                return;
            }
            m_client.command("UID COPY " + id + ' ' + target, [this, id, done](const ImapClient::Result &c) {
                if (!c.ok) {
                    done(c);
                    return;
                }
                m_client.command("UID STORE " + id + " +FLAGS.SILENT (\\Deleted)", [this, id, done](const ImapClient::Result &s) {
                    if (!s.ok || !m_client.hasCapability("UIDPLUS")) {
                        done(s);
                        return;
                    }
                    m_client.command("UID EXPUNGE " + id, done);
                });
            });
        };
        if (m_selected == path) {
            act();
            return;
        }
        select(path, [act, next](bool ok, const ImapClient::Result &) {
            if (ok)
                act();
            else
                next();
        });
    };
    enqueue(op);
    if (!trash.isEmpty() && trash != path)
        syncFolder(trash);
}

void Sync::keepSent(const QByteArray &raw)
{
    Op op;
    op.what = tr("Keeping a copy in Sent");
    op.run = [this, raw](std::function<void()> next) {
        const QByteArray sent = folderWithRole(QStringLiteral("sent"));
        bool ok = false;
        const QByteArray target = sent.isEmpty() ? QByteArray() : Imap::quoted(sent, &ok);
        if (sent.isEmpty() || !ok || m_client.hasCapability("X-GM-EXT-1")) { // Gmail keeps its own copy
            next();
            return;
        }
        m_client.commandWithLiteral("APPEND " + target + " (\\Seen)", raw, [this, sent, next](const ImapClient::Result &r) {
            if (!r.ok)
                emit error(tr("A copy couldn't be kept in Sent: %1").arg(r.text));
            next();
            syncFolder(sent);
        });
    };
    enqueue(op);
}
