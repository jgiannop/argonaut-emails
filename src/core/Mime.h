#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

// Reading a message as it travels (RFC 5322 headers, MIME: RFC 2045-2047,
// RFC 2231 parameters). Everything here is data from strangers: nothing is
// run, nothing is fetched, a broken message gives what could be read.
namespace Mime {

struct Address
{
    QString name;  // "Maria Papadopoulou" (may be empty)
    QString email; // "maria@example.com"

    // "Maria Papadopoulou <maria@example.com>", or the address alone.
    QString display() const;
    bool operator==(const Address &other) const { return name == other.name && email == other.email; }
};

struct Part
{
    QList<QPair<QByteArray, QByteArray>> headers; // raw names and unfolded values
    QByteArray type = "text/plain";               // lowercase "type/subtype"
    QMap<QByteArray, QString> params;             // lowercase names: charset, boundary, name…
    QByteArray disposition;                       // "inline", "attachment" or empty
    QString fileName;                             // from the disposition, else the type's name
    QByteArray contentId;                         // without the < >
    QByteArray body;                              // the transfer encoding undone (still in its charset)
    QList<Part> children;                         // a multipart's parts, or a message/rfc822's message

    // The first header of that name, unfolded, its encoded words decoded.
    QString header(const char *name) const;
    QByteArray rawHeader(const char *name) const;
    bool isMultipart() const { return type.startsWith("multipart/"); }
    bool isAttachment() const;
    // The body as text, in its charset (UTF-8, else Latin-1, when it says none or one we don't know).
    QString text() const;
};

struct Attachment
{
    QString fileName;
    QByteArray type;
    QByteArray contentId;
    qsizetype size = 0;
    const Part *part = nullptr; // into the Message it came from
};

struct Message
{
    Part root;

    QString subject() const { return root.header("Subject"); }
    QList<Address> from() const;
    QList<Address> to() const;
    QList<Address> cc() const;
    QList<Address> replyTo() const;
    QDateTime date() const;
    QByteArray messageId() const;  // without the < >
    QByteArray inReplyTo() const;
    QByteArray references() const; // as written

    // What to read: the plain text, and the HTML when there is one.
    QString plainText() const;
    QString html() const;
    QList<Attachment> attachments() const;
    // An inline part by its Content-ID (for cid: pictures in the HTML).
    const Part *partById(const QByteArray &contentId) const;
};

Message parse(const QByteArray &raw);
// The headers alone (what a list of messages is made of).
Part parseHeaders(const QByteArray &raw);

// Pieces, exposed for the tests and the composer.
QString decodeWords(const QByteArray &value); // "=?UTF-8?B?...?=" and friends
QByteArray decodeQuotedPrintable(const QByteArray &data, bool header = false);
QByteArray encodeQuotedPrintable(const QByteArray &data);
QList<Address> parseAddresses(const QString &value);
QDateTime parseDate(const QString &value);
QString decodeCharset(const QByteArray &data, const QByteArray &charset);

} // namespace Mime
