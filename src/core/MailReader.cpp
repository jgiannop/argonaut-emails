#include "MailReader.h"

#include "Account.h"
#include "Mime.h"
#include "Sync.h"

#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <memory>

MailReader::MailReader(MailStore *store, std::function<Sync *(const QString &)> syncFor, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_syncFor(std::move(syncFor))
{
}

QString MailReader::plainFromHtml(const QString &html)
{
    static const QRegularExpression hidden(QStringLiteral("<(style|script|head|title)\\b[^>]*>.*?</\\1\\s*>"),
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression breaks(QStringLiteral("<(br|/p|/div|/tr|/li|/h[1-6]|/table|/blockquote)\\b[^>]*>"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cells(QStringLiteral("</t[dh]\\s*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    static const QRegularExpression numeric(QStringLiteral("&#(x?)([0-9a-fA-F]{1,6});"));
    QString text = html;
    text.remove(comments);
    text.remove(hidden);
    text.replace(breaks, QStringLiteral("\n"));
    text.replace(cells, QStringLiteral(" "));
    text.remove(tags);
    // Entities: the common names, and numbers.
    QString out;
    out.reserve(text.size());
    qsizetype at = 0;
    QRegularExpressionMatchIterator it = numeric.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(text).mid(at, m.capturedStart() - at);
        bool ok = false;
        const uint code = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
        if (ok && code >= 0x20 && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF)) {
            const char32_t c = code;
            out += QString::fromUcs4(&c, 1);
        } else {
            out += QLatin1Char(' ');
        }
        at = m.capturedEnd();
    }
    out += QStringView(text).mid(at);
    const std::pair<const char *, const char *> named[] = {{"&nbsp;", " "}, {"&lt;", "<"},   {"&gt;", ">"},     {"&quot;", "\""},
                                                           {"&apos;", "'"}, {"&zwnj;", ""},  {"&shy;", ""},     {"&rsquo;", "’"},
                                                           {"&lsquo;", "‘"}, {"&ldquo;", "“"}, {"&rdquo;", "”"},
                                                           {"&ndash;", "–"}, {"&mdash;", "—"}, {"&hellip;", "…"},
                                                           {"&euro;", "€"}, {"&copy;", "©"}, {"&amp;", "&"}};
    for (const auto &[entity, character] : named)
        out.replace(QLatin1String(entity), QString::fromUtf8(character), Qt::CaseInsensitive);
    return out;
}

QString MailReader::textOf(const QByteArray &raw, int chars)
{
    const Mime::Message message = Mime::parse(raw);
    QString text = message.plainText();
    if (text.trimmed().isEmpty())
        text = plainFromHtml(message.html());
    // What is read: the message's own words, not the replies quoted under them.
    QStringList kept;
    int blank = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.simplified();
        if (line.startsWith(QLatin1Char('>')))
            continue;
        if (line.isEmpty()) {
            if (++blank > 1 || kept.isEmpty())
                continue;
        } else {
            blank = 0;
        }
        kept << line;
    }
    QString out = kept.join(QLatin1Char('\n')).trimmed();
    if (out.size() > chars)
        out = out.left(chars).trimmed() + QStringLiteral("…");
    return out;
}

void MailReader::read(const Query &query, Done done)
{
    struct Found
    {
        MailStore::Summary summary;
        QString folderName;
    };
    QList<Found> found;
    for (const Account &account : Account::all()) {
        for (const MailStore::Folder &folder : m_store->folders(account.id)) {
            if (folder.role != query.role)
                continue;
            for (const MailStore::Summary &s : m_store->messages(account.id, folder.path)) {
                if (s.date.isValid() && s.date >= query.since)
                    found.append({s, folder.name});
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return a.summary.date > b.summary.date; });
    if (found.size() > query.limit)
        found.resize(query.limit);

    // The messages as data, with their text when it is there (and asked for).
    auto make = [this, query, found]() {
        QJsonArray out;
        for (const Found &f : found) {
            const MailStore::Summary &s = f.summary;
            QJsonObject m{{QStringLiteral("account"), Account::byId(s.account).email},
                          {QStringLiteral("folder"), f.folderName},
                          {QStringLiteral("uid"), double(s.uid)},
                          {QStringLiteral("from"), s.fromName},
                          {QStringLiteral("fromEmail"), s.fromEmail},
                          {QStringLiteral("to"), s.to},
                          {QStringLiteral("subject"), s.subject},
                          {QStringLiteral("date"), s.date.toString(Qt::ISODate)},
                          {QStringLiteral("unread"), !s.seen},
                          {QStringLiteral("starred"), s.flagged},
                          {QStringLiteral("attachments"), s.attachments}};
            if (!s.categories.isEmpty())
                m.insert(QStringLiteral("tabs"), QJsonArray::fromStringList(s.categories));
            if (query.text) {
                const QByteArray raw = m_store->body(s.account, s.folder, s.uid);
                if (!raw.isEmpty())
                    m.insert(QStringLiteral("text"), textOf(raw, query.chars));
            }
            out.append(m);
        }
        return out;
    };
    if (!query.text) {
        done(make(), QString());
        return;
    }

    // What isn't kept yet is fetched, a folder at a time, by its account's sync.
    QHash<QPair<QString, QByteArray>, QList<qint64>> missing;
    for (const Found &f : std::as_const(found)) {
        const MailStore::Summary &s = f.summary;
        if (s.size <= MaxFetchBytes && m_store->body(s.account, s.folder, s.uid).isEmpty() && m_syncFor(s.account))
            missing[{s.account, s.folder}].append(s.uid);
    }
    if (missing.isEmpty()) {
        done(make(), QString());
        return;
    }
    auto left = std::make_shared<int>(int(missing.size()));
    auto errors = std::make_shared<QStringList>();
    for (auto it = missing.constBegin(); it != missing.constEnd(); ++it) {
        Sync *sync = m_syncFor(it.key().first);
        sync->fetchBodies(it.key().second, it.value(), [left, errors, make, done](const QString &error) {
            if (!error.isEmpty())
                errors->append(error);
            if (--*left == 0)
                done(make(), errors->isEmpty() ? QString() : errors->first());
        });
    }
}
