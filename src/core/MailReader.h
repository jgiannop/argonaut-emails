#pragma once

#include "MailStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QObject>

#include <functional>

class Sync;

// The user's messages as an extension may read them ("uses": ["mail"]):
// those of a kind of folder (the inbox, Sent…) of every account since a
// time, newest first, as plain data: who, to whom, when, the subject, and,
// when asked, the beginning of the text. Never attachments, never the raw
// message, never the HTML (what is in it is turned into plain text here).
//
// The text of a message that was never opened is fetched (and kept, as when
// it is opened); a very large one is left without text.
class MailReader : public QObject
{
    Q_OBJECT

public:
    struct Query
    {
        QString role = QStringLiteral("inbox"); // a folder's role: inbox, sent, all, starred, archive…
        QDateTime since;
        int limit = 300;
        bool text = false;
        int chars = 1500; // of each message's text
    };
    using Done = std::function<void(const QJsonArray &messages, const QString &error)>;

    static constexpr qint64 MaxFetchBytes = 1024 * 1024; // larger messages go without their text

    MailReader(MailStore *store, std::function<Sync *(const QString &account)> syncFor, QObject *parent = nullptr);

    void read(const Query &query, Done done);

    // The readable text of a message, at most `chars` characters: its plain
    // text, else its HTML made plain; quoted replies and long blank runs out.
    static QString textOf(const QByteArray &raw, int chars);
    // HTML as plain text (styles, scripts and tags out, entities read).
    static QString plainFromHtml(const QString &html);

private:
    MailStore *m_store;
    std::function<Sync *(const QString &)> m_syncFor;
};
