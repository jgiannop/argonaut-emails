#pragma once

#include "Mime.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

// Writing a message to send (RFC 5322 + MIME): plain text in UTF-8
// (quoted-printable), attachments in base64 under multipart/mixed, names
// and subjects with non-ASCII letters as encoded words.
namespace Composer {

struct File
{
    QString name;
    QByteArray type; // "application/pdf"; empty: from the name
    QByteArray data;
};

struct Draft
{
    Mime::Address from;
    QList<Mime::Address> to;
    QList<Mime::Address> cc;
    QList<Mime::Address> bcc; // only in the envelope, never in the message
    QString subject;
    QString text;
    QList<File> files;
    QByteArray inReplyTo;  // a reply: the Message-ID it answers
    QByteArray references; // and the thread before it
    QDateTime date;        // empty: now
    QByteArray messageId;  // empty: made here
};

QByteArray build(const Draft &draft);
// Everyone it goes to: To, Cc and Bcc, each address once.
QStringList recipients(const Draft &draft);

// A header value with encoded words where it needs them, folded at 76.
QByteArray encodeHeader(const QString &value);
QByteArray encodeAddresses(const QList<Mime::Address> &addresses);

// What a reply or a forward starts with.
QString replySubject(const QString &subject);   // "Re: …" once
QString forwardSubject(const QString &subject); // "Fwd: …"
QString quoted(const Mime::Message &message);   // "On …, Maria wrote:" and "> " lines
QString forwardedText(const Mime::Message &message);

} // namespace Composer
