#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// The IMAP4rev1 wire format (RFC 3501): what the server sends, as values.
//
// A response is one line, except that a literal ({12}\r\n then 12 bytes)
// can carry any bytes, line breaks included, and the line goes on after it.
// `takeResponse` cuts one whole response off the front of what has been
// received; `parseResponse` turns it into a tag and values.
namespace Imap {

struct Value
{
    enum Kind { Atom, String, List, Nil };
    Kind kind = Nil;
    QByteArray text;    // an atom or a string
    QList<Value> items; // a list

    bool isList() const { return kind == List; }
    bool isNil() const { return kind == Nil; }
    // The text of an atom or a string, uppercase compared as IMAP does.
    bool is(const char *atom) const { return kind == Atom && text.compare(atom, Qt::CaseInsensitive) == 0; }
    // An atom's or string's text; empty for NIL and lists.
    QByteArray bytes() const { return kind == Atom || kind == String ? text : QByteArray(); }
    QString string() const { return QString::fromUtf8(bytes()); }
};

struct Response
{
    QByteArray tag;      // "*" (untagged), "+" (go on), or the command's tag
    QList<Value> values; // what follows the tag, tokenized
    // A status response (OK, NO, BAD, BYE, PREAUTH): its word, the
    // bracketed code ("UIDVALIDITY 42", "ALERT") and the human text.
    QByteArray status;
    QByteArray code;
    QString text;

    bool isUntagged() const { return tag == "*"; }
    bool isContinuation() const { return tag == "+"; }
    bool isStatus() const { return !status.isEmpty(); }
};

// One whole response off the front of `buffer` (literals included), into
// `out`. False until all of it has arrived. A response over `maxBytes`
// sets `tooBig`: the connection has to be dropped.
bool takeResponse(QByteArray &buffer, QByteArray *out, bool *tooBig = nullptr, qsizetype maxBytes = 64 * 1024 * 1024);

Response parseResponse(const QByteArray &response);
// The values of a line without a tag (exposed for tests).
QList<Value> parseValues(const QByteArray &text);

// A string to send: an atom-safe word as it is, else quoted. Returns empty
// (and false in `ok`) for text that can't be quoted (CR, LF, NUL).
QByteArray quoted(const QByteArray &text, bool *ok = nullptr);

// Folder names travel in modified UTF-7 (RFC 3501 5.1.3): "Entw&APw-rfe".
QString decodeMailboxName(const QByteArray &name);
QByteArray encodeMailboxName(const QString &name);

// FETCH data ("UID 4 FLAGS (\Seen) BODY[] {..}"): the value after `key`.
const Value *fetchItem(const Value &list, const char *key);

} // namespace Imap
