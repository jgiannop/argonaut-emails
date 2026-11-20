#include "Mime.h"

#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>

namespace Mime {

namespace {

constexpr int MaxDepth = 20;    // nested multiparts
constexpr int MaxParts = 1000;  // in one message

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Bytes as text when nobody said which charset: UTF-8 if they are, else Latin-1.
QString guessText(const QByteArray &bytes)
{
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = utf8.decode(bytes);
    if (!utf8.hasError())
        return text;
    return QString::fromLatin1(bytes);
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

QByteArray percentDecode(const QByteArray &text)
{
    QByteArray out;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == '%' && i + 2 < text.size() + 0 && hexValue(text.at(i + 1)) >= 0 && hexValue(text.at(i + 2)) >= 0) {
            out.append(char(hexValue(text.at(i + 1)) * 16 + hexValue(text.at(i + 2))));
            i += 2;
        } else {
            out.append(text.at(i));
        }
    }
    return out;
}

// "a; b=c; d=\"e;f\"" split at the semicolons outside quotes.
QList<QByteArray> splitParams(const QByteArray &value)
{
    QList<QByteArray> out;
    QByteArray current;
    bool inQuotes = false;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const char c = value.at(i);
        if (c == '\\' && inQuotes && i + 1 < value.size()) {
            current.append(c).append(value.at(++i));
            continue;
        }
        if (c == '"')
            inQuotes = !inQuotes;
        if (c == ';' && !inQuotes) {
            out.append(current.trimmed());
            current.clear();
            continue;
        }
        current.append(c);
    }
    out.append(current.trimmed());
    return out;
}

QByteArray unquote(QByteArray value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
        QByteArray out;
        for (qsizetype i = 1; i + 1 < value.size(); ++i) {
            if (value.at(i) == '\\' && i + 2 < value.size())
                ++i;
            out.append(value.at(i));
        }
        return out;
    }
    return value;
}

// The value before the first ';' (lowercase) and the parameters after it,
// RFC 2231 continuations and charsets included.
QByteArray parseParams(const QByteArray &header, QMap<QByteArray, QString> *params)
{
    const QList<QByteArray> pieces = splitParams(header);
    struct Piece
    {
        int index = 0;
        bool extended = false;
        QByteArray value;
    };
    QMap<QByteArray, QList<Piece>> continued;
    for (qsizetype i = 1; i < pieces.size(); ++i) {
        const qsizetype eq = pieces.at(i).indexOf('=');
        if (eq <= 0)
            continue;
        QByteArray name = pieces.at(i).left(eq).trimmed().toLower();
        const QByteArray value = unquote(pieces.at(i).mid(eq + 1));
        if (!name.contains('*')) {
            if (!params->contains(name))
                params->insert(name, decodeWords(value));
            continue;
        }
        Piece piece;
        piece.extended = name.endsWith('*');
        if (piece.extended)
            name.chop(1);
        const qsizetype star = name.indexOf('*');
        if (star > 0) {
            piece.index = name.mid(star + 1).toInt();
            name = name.left(star);
        }
        piece.value = value;
        continued[name].append(piece);
    }
    for (auto it = continued.begin(); it != continued.end(); ++it) {
        QList<Piece> list = it.value();
        std::sort(list.begin(), list.end(), [](const Piece &a, const Piece &b) { return a.index < b.index; });
        QByteArray charset;
        QByteArray bytes;
        for (int k = 0; k < list.size(); ++k) {
            QByteArray value = list.at(k).value;
            if (list.at(k).extended) {
                if (k == 0) { // charset'language'text
                    const qsizetype first = value.indexOf('\'');
                    const qsizetype second = first < 0 ? -1 : value.indexOf('\'', first + 1);
                    if (second > 0) {
                        charset = value.left(first);
                        value = value.mid(second + 1);
                    }
                }
                bytes += percentDecode(value);
            } else {
                bytes += value;
            }
        }
        params->insert(it.key(), charset.isEmpty() ? guessText(bytes) : decodeCharset(bytes, charset));
    }
    return pieces.value(0).trimmed().toLower();
}

void parseHeaderBlock(const QByteArray &block, Part *part)
{
    QByteArray name;
    QByteArray value;
    auto flush = [&] {
        if (!name.isEmpty())
            part->headers.append({name, value.trimmed()});
        name.clear();
        value.clear();
    };
    for (QByteArray line : block.split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if (isSpace(line.at(0))) { // a folded line goes on the one before
            if (!name.isEmpty())
                value += ' ' + line.trimmed();
            continue;
        }
        flush();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            continue; // not a header (a "From " line of mbox): left out
        name = line.left(colon).trimmed();
        value = line.mid(colon + 1);
    }
    flush();
}

// Where the headers end: the first empty line.
void splitHeaders(const QByteArray &raw, QByteArray *headers, QByteArray *body)
{
    qsizetype end = -1;
    qsizetype bodyStart = raw.size();
    for (qsizetype i = 0; i < raw.size(); ++i) {
        if (raw.at(i) != '\n')
            continue;
        if (i + 1 < raw.size() && raw.at(i + 1) == '\n') {
            end = i;
            bodyStart = i + 2;
            break;
        }
        if (i + 2 < raw.size() && raw.at(i + 1) == '\r' && raw.at(i + 2) == '\n') {
            end = i;
            bodyStart = i + 3;
            break;
        }
    }
    if (raw.startsWith("\r\n") || raw.startsWith('\n')) { // no headers at all
        *headers = QByteArray();
        *body = raw.mid(raw.startsWith("\r\n") ? 2 : 1);
        return;
    }
    *headers = end < 0 ? raw : raw.left(end);
    *body = end < 0 ? QByteArray() : raw.mid(bodyStart);
}

void fillTypeFields(Part *part)
{
    QMap<QByteArray, QString> typeParams;
    const QByteArray contentType = part->rawHeader("Content-Type");
    QByteArray type = contentType.isEmpty() ? QByteArray() : parseParams(contentType, &typeParams);
    if (!type.contains('/'))
        type = "text/plain";
    part->type = type;
    part->params = typeParams;
    QMap<QByteArray, QString> dispositionParams;
    const QByteArray disposition = part->rawHeader("Content-Disposition");
    if (!disposition.isEmpty())
        part->disposition = parseParams(disposition, &dispositionParams);
    part->fileName = dispositionParams.value("filename");
    if (part->fileName.isEmpty())
        part->fileName = typeParams.value("name");
    // Only the name: never a path someone could make us write to.
    part->fileName.replace(QLatin1Char('\\'), QLatin1Char('/'));
    part->fileName = part->fileName.section(QLatin1Char('/'), -1).trimmed();
    QByteArray id = part->rawHeader("Content-ID").trimmed();
    if (id.startsWith('<') && id.endsWith('>'))
        id = id.mid(1, id.size() - 2);
    part->contentId = id;
}

QByteArray undoTransferEncoding(const QByteArray &body, const QByteArray &encoding)
{
    const QByteArray e = encoding.trimmed().toLower();
    if (e == "base64") {
        QByteArray clean;
        clean.reserve(body.size());
        for (const char c : body) {
            if (!isSpace(c))
                clean.append(c);
        }
        return QByteArray::fromBase64(clean);
    }
    if (e == "quoted-printable")
        return decodeQuotedPrintable(body);
    return body;
}

void parsePart(const QByteArray &raw, Part *part, int depth, int *count);

void parseMultipart(const QByteArray &body, Part *part, int depth, int *count)
{
    const QByteArray boundary = part->params.value("boundary").toUtf8();
    if (boundary.isEmpty())
        return;
    const QByteArray delimiter = "--" + boundary;
    QList<qsizetype> starts; // where each delimiter line starts
    qsizetype from = 0;
    bool closed = false;
    qsizetype closeAt = -1;
    while (true) {
        const qsizetype at = body.indexOf(delimiter, from);
        if (at < 0)
            break;
        if (at == 0 || body.at(at - 1) == '\n') {
            const qsizetype after = at + delimiter.size();
            if (body.mid(after, 2) == "--") {
                closed = true;
                closeAt = at;
                break;
            }
            starts.append(at);
        }
        from = at + delimiter.size();
    }
    Q_UNUSED(closed)
    for (int i = 0; i < starts.size(); ++i) {
        if (*count >= MaxParts)
            return;
        qsizetype begin = body.indexOf('\n', starts.at(i));
        if (begin < 0)
            break;
        ++begin;
        qsizetype end = i + 1 < starts.size() ? starts.at(i + 1) : (closeAt >= 0 ? closeAt : body.size());
        // The line break before the next delimiter belongs to it.
        if (end > begin && body.at(end - 1) == '\n')
            --end;
        if (end > begin && body.at(end - 1) == '\r')
            --end;
        Part child;
        parsePart(body.mid(begin, qMax<qsizetype>(0, end - begin)), &child, depth + 1, count);
        part->children.append(child);
    }
}

void parsePart(const QByteArray &raw, Part *part, int depth, int *count)
{
    ++*count;
    QByteArray headers;
    QByteArray body;
    splitHeaders(raw, &headers, &body);
    parseHeaderBlock(headers, part);
    fillTypeFields(part);
    if (part->isMultipart() && depth < MaxDepth) {
        parseMultipart(body, part, depth, count);
        return;
    }
    part->body = undoTransferEncoding(body, part->rawHeader("Content-Transfer-Encoding"));
    if (part->type == "message/rfc822" && depth < MaxDepth && *count < MaxParts) {
        Part inner;
        parsePart(part->body, &inner, depth + 1, count);
        part->children.append(inner);
    }
}

const Part *findBody(const Part &part, const QByteArray &type)
{
    if (part.isMultipart()) {
        for (const Part &child : part.children) {
            if (const Part *found = findBody(child, type))
                return found;
        }
        return nullptr;
    }
    if (part.type == type && !part.isAttachment())
        return &part;
    return nullptr;
}

void collectAttachments(const Part &part, QList<Attachment> *out)
{
    if (part.isMultipart()) {
        for (const Part &child : part.children)
            collectAttachments(child, out);
        return;
    }
    if (!part.isAttachment())
        return;
    Attachment a;
    a.fileName = part.fileName;
    if (a.fileName.isEmpty())
        a.fileName = part.type == "message/rfc822" ? QStringLiteral("message.eml") : QStringLiteral("attachment");
    a.type = part.type;
    a.contentId = part.contentId;
    a.size = part.body.size();
    a.part = &part;
    out->append(a);
}

const Part *findById(const Part &part, const QByteArray &id)
{
    if (!part.contentId.isEmpty() && part.contentId == id)
        return &part;
    for (const Part &child : part.children) {
        if (const Part *found = findById(child, id))
            return found;
    }
    return nullptr;
}

QByteArray firstAngle(const QByteArray &value)
{
    const qsizetype open = value.indexOf('<');
    const qsizetype close = value.indexOf('>', open + 1);
    if (open < 0 || close < 0)
        return value.trimmed();
    return value.mid(open + 1, close - open - 1).trimmed();
}

QList<Address> addressesOf(const Part &part, const char *name)
{
    // Split the raw value first: an encoded name may hold a comma.
    QList<Address> list = parseAddresses(QString::fromLatin1(part.rawHeader(name)));
    for (Address &a : list) {
        a.name = decodeWords(a.name.toLatin1());
        a.email = guessText(a.email.toLatin1());
    }
    return list;
}

} // namespace

QString Address::display() const
{
    if (name.isEmpty())
        return email;
    if (email.isEmpty())
        return name;
    return QStringLiteral("%1 <%2>").arg(name, email);
}

QByteArray Part::rawHeader(const char *name) const
{
    for (const auto &header : headers) {
        if (header.first.compare(name, Qt::CaseInsensitive) == 0)
            return header.second;
    }
    return {};
}

QString Part::header(const char *name) const
{
    return decodeWords(rawHeader(name));
}

bool Part::isAttachment() const
{
    if (isMultipart())
        return false;
    if (disposition == "attachment")
        return true;
    if (type == "text/plain" || type == "text/html")
        return !fileName.isEmpty() && disposition != "inline";
    return true; // pictures, documents, forwarded messages…
}

QString Part::text() const
{
    const QByteArray charset = params.value("charset").toLatin1();
    if (charset.isEmpty())
        return guessText(body);
    return decodeCharset(body, charset);
}

QString decodeCharset(const QByteArray &data, const QByteArray &charset)
{
    QByteArray name = charset.trimmed().toLower();
    if (const qsizetype star = name.indexOf('*'); star > 0) // RFC 2231 language
        name = name.left(star);
    if (name == "us-ascii" || name == "ascii" || name.isEmpty())
        return guessText(data);
    QStringDecoder decoder(name.constData());
    if (!decoder.isValid())
        return guessText(data);
    return decoder.decode(data);
}

QString decodeWords(const QByteArray &value)
{
    // Pieces: encoded words and the plain text between them.
    struct Piece
    {
        bool encoded = false;
        QString text;
        QByteArray raw;
    };
    QList<Piece> pieces;
    QByteArray plain;
    auto flushPlain = [&] {
        if (!plain.isEmpty())
            pieces.append({false, QString(), plain});
        plain.clear();
    };
    qsizetype i = 0;
    while (i < value.size()) {
        if (value.at(i) == '=' && i + 1 < value.size() && value.at(i + 1) == '?') {
            // =?charset?E?text?=
            const qsizetype q1 = value.indexOf('?', i + 2);
            const qsizetype q2 = q1 < 0 ? -1 : value.indexOf('?', q1 + 1);
            const qsizetype end = q2 < 0 ? -1 : value.indexOf("?=", q2 + 1);
            if (q1 > 0 && q2 == q1 + 2 && end > 0) {
                const QByteArray charset = value.mid(i + 2, q1 - i - 2);
                const char encoding = char(QChar::toUpper(uint(uchar(value.at(q1 + 1)))));
                const QByteArray text = value.mid(q2 + 1, end - q2 - 1);
                if (encoding == 'B' || encoding == 'Q') {
                    const QByteArray bytes = encoding == 'B' ? QByteArray::fromBase64(text) : decodeQuotedPrintable(text, true);
                    flushPlain();
                    pieces.append({true, decodeCharset(bytes, charset), QByteArray()});
                    i = end + 2;
                    continue;
                }
            }
        }
        plain.append(value.at(i));
        ++i;
    }
    flushPlain();
    QString out;
    for (qsizetype k = 0; k < pieces.size(); ++k) {
        const Piece &piece = pieces.at(k);
        if (piece.encoded) {
            out += piece.text;
            continue;
        }
        // Space between two encoded words is not part of the text.
        const bool between = k > 0 && k + 1 < pieces.size() && pieces.at(k - 1).encoded && pieces.at(k + 1).encoded;
        if (between && std::all_of(piece.raw.begin(), piece.raw.end(), isSpace))
            continue;
        out += guessText(piece.raw);
    }
    return out;
}

QByteArray decodeQuotedPrintable(const QByteArray &data, bool header)
{
    QByteArray out;
    out.reserve(data.size());
    for (qsizetype i = 0; i < data.size(); ++i) {
        const char c = data.at(i);
        if (header && c == '_') {
            out.append(' ');
        } else if (c == '=') {
            if (i + 2 < data.size() + 0 && hexValue(data.at(i + 1)) >= 0 && hexValue(data.at(i + 2)) >= 0) {
                out.append(char(hexValue(data.at(i + 1)) * 16 + hexValue(data.at(i + 2))));
                i += 2;
            } else {
                // A soft line break: "=" then (spaces and) the end of the line.
                qsizetype j = i + 1;
                while (j < data.size() && (data.at(j) == ' ' || data.at(j) == '\t'))
                    ++j;
                if (j < data.size() && data.at(j) == '\r')
                    ++j;
                if (j < data.size() && data.at(j) == '\n') {
                    i = j;
                } else if (j >= data.size()) {
                    i = j;
                } else {
                    out.append(c); // a stray "=": kept
                }
            }
        } else {
            out.append(c);
        }
    }
    return out;
}

QByteArray encodeQuotedPrintable(const QByteArray &data)
{
    static const char hex[] = "0123456789ABCDEF";
    QByteArray out;
    int column = 0;
    auto put = [&](const QByteArray &piece) {
        if (column + piece.size() > 75) {
            out.append("=\r\n");
            column = 0;
        }
        out.append(piece);
        column += piece.size();
    };
    QByteArray text = data;
    text.replace("\r\n", "\n");
    for (qsizetype i = 0; i < text.size(); ++i) {
        const uchar c = uchar(text.at(i));
        if (c == '\n') {
            out.append("\r\n");
            column = 0;
            continue;
        }
        const bool lineEndsHere = i + 1 >= text.size() || text.at(i + 1) == '\n';
        const bool plainChar = (c >= 33 && c <= 126 && c != '=') || ((c == ' ' || c == '\t') && !lineEndsHere);
        // A line that starts with "." or "From " is left alone by SMTP, but some
        // tools mangle it: the first character is encoded.
        const bool lineStart = column == 0 && (c == '.' || (c == 'F' && text.mid(i, 5) == "From "));
        if (plainChar && !lineStart)
            put(QByteArray(1, char(c)));
        else
            put(QByteArray{"=", 1} + hex[c >> 4] + hex[c & 15]);
    }
    return out;
}

QList<Address> parseAddresses(const QString &value)
{
    QList<Address> out;
    QString current;
    int angle = 0;
    int comment = 0;
    bool quotes = false;
    auto finish = [&] {
        const QString entry = current.trimmed();
        current.clear();
        if (entry.isEmpty())
            return;
        Address a;
        const qsizetype open = entry.lastIndexOf(QLatin1Char('<'));
        const qsizetype close = entry.indexOf(QLatin1Char('>'), open + 1);
        QString name;
        if (open >= 0 && close > open) {
            a.email = entry.mid(open + 1, close - open - 1).trimmed();
            name = entry.left(open).trimmed();
        } else {
            // "maria@example.com (Maria)": the comment is the name.
            QString rest = entry;
            const qsizetype c1 = rest.indexOf(QLatin1Char('('));
            const qsizetype c2 = rest.lastIndexOf(QLatin1Char(')'));
            if (c1 >= 0 && c2 > c1) {
                name = rest.mid(c1 + 1, c2 - c1 - 1).trimmed();
                rest.remove(c1, c2 - c1 + 1);
            }
            a.email = rest.trimmed();
        }
        if (name.size() >= 2 && name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"'))) {
            QString unquoted;
            for (qsizetype k = 1; k + 1 < name.size(); ++k) {
                if (name.at(k) == QLatin1Char('\\') && k + 2 < name.size())
                    ++k;
                unquoted.append(name.at(k));
            }
            name = unquoted;
        }
        a.name = name.trimmed();
        if (a.email.startsWith(QLatin1Char('"')) && a.email.endsWith(QLatin1Char('"')) && a.email.size() >= 2)
            a.email = a.email.mid(1, a.email.size() - 2);
        if (!a.email.isEmpty() || !a.name.isEmpty())
            out.append(a);
    };
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (quotes) {
            current.append(c);
            if (c == QLatin1Char('\\') && i + 1 < value.size())
                current.append(value.at(++i));
            else if (c == QLatin1Char('"'))
                quotes = false;
            continue;
        }
        if (c == QLatin1Char('"')) {
            quotes = true;
        } else if (c == QLatin1Char('(')) {
            ++comment;
        } else if (c == QLatin1Char(')') && comment > 0) {
            --comment;
        } else if (comment == 0 && c == QLatin1Char('<')) {
            ++angle;
        } else if (comment == 0 && c == QLatin1Char('>') && angle > 0) {
            --angle;
        } else if (comment == 0 && angle == 0 && c == QLatin1Char(':')) {
            current.clear(); // "Friends: a@x, b@y;": the group's name isn't an address
            continue;
        } else if (comment == 0 && angle == 0 && (c == QLatin1Char(',') || c == QLatin1Char(';'))) {
            finish();
            continue;
        }
        current.append(c);
    }
    finish();
    return out;
}

QDateTime parseDate(const QString &value)
{
    QString text = value;
    // Comments: "(UTC)", "(EEST)".
    static const QRegularExpression comments(QStringLiteral("\\([^)]*\\)"));
    text.remove(comments);
    text = text.simplified();
    QDateTime date = QDateTime::fromString(text, Qt::RFC2822Date);
    if (date.isValid())
        return date;
    static const QRegularExpression zones(QStringLiteral("\\s(GMT|UTC|UT|Z)$"), QRegularExpression::CaseInsensitiveOption);
    text.replace(zones, QStringLiteral(" +0000"));
    date = QDateTime::fromString(text, Qt::RFC2822Date);
    if (date.isValid())
        return date;
    // Without the day's name.
    const qsizetype comma = text.indexOf(QLatin1Char(','));
    if (comma >= 0)
        date = QDateTime::fromString(text.mid(comma + 1).trimmed(), Qt::RFC2822Date);
    return date;
}

Message parse(const QByteArray &raw)
{
    Message m;
    int count = 0;
    parsePart(raw, &m.root, 0, &count);
    return m;
}

Part parseHeaders(const QByteArray &raw)
{
    Part part;
    QByteArray headers;
    QByteArray body;
    splitHeaders(raw, &headers, &body);
    parseHeaderBlock(headers, &part);
    fillTypeFields(&part);
    return part;
}

QList<Address> Message::from() const
{
    return addressesOf(root, "From");
}

QList<Address> Message::to() const
{
    return addressesOf(root, "To");
}

QList<Address> Message::cc() const
{
    return addressesOf(root, "Cc");
}

QList<Address> Message::replyTo() const
{
    return addressesOf(root, "Reply-To");
}

QDateTime Message::date() const
{
    return parseDate(root.header("Date"));
}

QByteArray Message::messageId() const
{
    return firstAngle(root.rawHeader("Message-ID"));
}

QByteArray Message::inReplyTo() const
{
    return firstAngle(root.rawHeader("In-Reply-To"));
}

QByteArray Message::references() const
{
    return root.rawHeader("References");
}

QString Message::plainText() const
{
    const Part *part = findBody(root, "text/plain");
    return part ? part->text() : QString();
}

QString Message::html() const
{
    const Part *part = findBody(root, "text/html");
    return part ? part->text() : QString();
}

QList<Attachment> Message::attachments() const
{
    QList<Attachment> out;
    collectAttachments(root, &out);
    return out;
}

const Part *Message::partById(const QByteArray &contentId) const
{
    return contentId.isEmpty() ? nullptr : findById(root, contentId);
}

} // namespace Mime
