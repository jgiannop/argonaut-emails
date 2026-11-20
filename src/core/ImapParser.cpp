#include "ImapParser.h"

namespace Imap {

namespace {

// "{123}" or "{123+}" at the end of a line: the literal's length, else -1.
qsizetype literalAtEnd(const QByteArray &buffer, qsizetype lineEnd)
{
    if (lineEnd == 0 || buffer.at(lineEnd - 1) != '}')
        return -1;
    qsizetype i = lineEnd - 2;
    if (i >= 0 && buffer.at(i) == '+')
        --i;
    const qsizetype digitsEnd = i + 1;
    while (i >= 0 && buffer.at(i) >= '0' && buffer.at(i) <= '9')
        --i;
    if (i < 0 || buffer.at(i) != '{' || i + 1 == digitsEnd || digitsEnd - (i + 1) > 12)
        return -1;
    return buffer.mid(i + 1, digitsEnd - (i + 1)).toLongLong();
}

class Reader
{
public:
    explicit Reader(const QByteArray &text)
        : m_text(text)
    {
    }

    bool atEnd() const { return m_pos >= m_text.size(); }

    QList<Value> values(bool untilClose)
    {
        QList<Value> out;
        while (true) {
            skipSpaces();
            if (atEnd())
                return out;
            const char c = m_text.at(m_pos);
            if (c == ')') {
                if (untilClose) {
                    ++m_pos;
                    return out;
                }
                ++m_pos; // a stray one: ignored
                continue;
            }
            if (c == '\r' || c == '\n') {
                ++m_pos;
                continue;
            }
            out.append(value());
        }
    }

private:
    void skipSpaces()
    {
        while (!atEnd() && m_text.at(m_pos) == ' ')
            ++m_pos;
    }

    Value value()
    {
        Value v;
        const char c = m_text.at(m_pos);
        if (c == '(') {
            ++m_pos;
            v.kind = Value::List;
            v.items = values(true);
            return v;
        }
        if (c == '"') {
            ++m_pos;
            v.kind = Value::String;
            while (!atEnd()) {
                char d = m_text.at(m_pos++);
                if (d == '\\' && !atEnd())
                    d = m_text.at(m_pos++);
                else if (d == '"')
                    break;
                v.text.append(d);
            }
            return v;
        }
        if (c == '{') {
            const qsizetype close = m_text.indexOf('}', m_pos);
            if (close > 0) {
                QByteArray digits = m_text.mid(m_pos + 1, close - m_pos - 1);
                if (digits.endsWith('+'))
                    digits.chop(1);
                bool ok = false;
                const qsizetype length = digits.toLongLong(&ok);
                qsizetype start = close + 1;
                if (start < m_text.size() && m_text.at(start) == '\r')
                    ++start;
                if (start < m_text.size() && m_text.at(start) == '\n')
                    ++start;
                if (ok && length >= 0) {
                    v.kind = Value::String;
                    v.text = m_text.mid(start, length);
                    m_pos = qMin(m_text.size(), start + length);
                    return v;
                }
            }
        }
        // An atom; brackets are part of it ("BODY[HEADER.FIELDS (FROM)]<0>").
        v.kind = Value::Atom;
        int depth = 0;
        while (!atEnd()) {
            const char d = m_text.at(m_pos);
            if (d == '[')
                ++depth;
            else if (d == ']' && depth > 0)
                --depth;
            else if (depth == 0 && (d == ' ' || d == '(' || d == ')' || d == '\r' || d == '\n'))
                break;
            v.text.append(d);
            ++m_pos;
        }
        if (v.text.isEmpty()) { // nothing an atom can be made of: step over it
            v.text.append(m_text.at(m_pos++));
        }
        if (v.text.compare("NIL", Qt::CaseInsensitive) == 0) {
            v.kind = Value::Nil;
            v.text.clear();
        }
        return v;
    }

    const QByteArray &m_text;
    qsizetype m_pos = 0;
};

bool isStatusWord(const QByteArray &word)
{
    for (const char *s : {"OK", "NO", "BAD", "BYE", "PREAUTH"}) {
        if (word.compare(s, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

} // namespace

bool takeResponse(QByteArray &buffer, QByteArray *out, bool *tooBig, qsizetype maxBytes)
{
    if (tooBig)
        *tooBig = false;
    qsizetype from = 0;
    while (true) {
        const qsizetype newline = buffer.indexOf('\n', from);
        if (newline < 0) {
            if (buffer.size() > maxBytes && tooBig)
                *tooBig = true;
            return false;
        }
        const qsizetype lineEnd = newline > 0 && buffer.at(newline - 1) == '\r' ? newline - 1 : newline;
        const qsizetype literal = literalAtEnd(buffer, lineEnd);
        if (literal < 0) {
            *out = buffer.left(lineEnd);
            buffer.remove(0, newline + 1);
            return true;
        }
        if (newline + 1 + literal > maxBytes) {
            if (tooBig)
                *tooBig = true;
            return false;
        }
        if (buffer.size() < newline + 1 + literal)
            return false; // the literal is still coming
        from = newline + 1 + literal;
    }
}

QList<Value> parseValues(const QByteArray &text)
{
    Reader reader(text);
    return reader.values(false);
}

Response parseResponse(const QByteArray &response)
{
    Response r;
    const qsizetype space = response.indexOf(' ');
    r.tag = space < 0 ? response : response.left(space);
    const QByteArray rest = space < 0 ? QByteArray() : response.mid(space + 1);
    if (r.isContinuation()) {
        r.text = QString::fromUtf8(rest);
        return r;
    }
    const qsizetype wordEnd = rest.indexOf(' ');
    const QByteArray first = wordEnd < 0 ? rest : rest.left(wordEnd);
    if (isStatusWord(first)) {
        r.status = first.toUpper();
        QByteArray text = wordEnd < 0 ? QByteArray() : rest.mid(wordEnd + 1);
        if (text.startsWith('[')) {
            const qsizetype close = text.indexOf(']');
            if (close > 0) {
                r.code = text.mid(1, close - 1);
                text = text.mid(close + 1).trimmed();
            }
        }
        r.text = QString::fromUtf8(text);
        return r;
    }
    r.values = parseValues(rest);
    return r;
}

QByteArray quoted(const QByteArray &text, bool *ok)
{
    if (ok)
        *ok = true;
    if (text.contains('\r') || text.contains('\n') || text.contains('\0')) {
        if (ok)
            *ok = false;
        return {};
    }
    QByteArray out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out.append('\\');
        out.append(c);
    }
    out.append('"');
    return out;
}

QString decodeMailboxName(const QByteArray &name)
{
    QString out;
    qsizetype i = 0;
    while (i < name.size()) {
        const char c = name.at(i);
        if (c != '&') {
            out.append(QLatin1Char(c));
            ++i;
            continue;
        }
        const qsizetype end = name.indexOf('-', i + 1);
        if (end < 0) { // broken: keep it as it is
            out.append(QString::fromLatin1(name.mid(i)));
            break;
        }
        if (end == i + 1) {
            out.append(QLatin1Char('&'));
        } else {
            QByteArray b64 = name.mid(i + 1, end - i - 1);
            b64.replace(',', '/');
            const QByteArray utf16 = QByteArray::fromBase64(b64);
            for (qsizetype k = 0; k + 1 < utf16.size(); k += 2)
                out.append(QChar(ushort((uchar(utf16.at(k)) << 8) | uchar(utf16.at(k + 1)))));
        }
        i = end + 1;
    }
    return out;
}

QByteArray encodeMailboxName(const QString &name)
{
    QByteArray out;
    qsizetype i = 0;
    while (i < name.size()) {
        const QChar c = name.at(i);
        if (c.unicode() >= 0x20 && c.unicode() <= 0x7e) {
            out.append(c == QLatin1Char('&') ? QByteArray("&-") : QByteArray(1, char(c.unicode())));
            ++i;
            continue;
        }
        QByteArray utf16;
        while (i < name.size() && !(name.at(i).unicode() >= 0x20 && name.at(i).unicode() <= 0x7e)) {
            utf16.append(char(name.at(i).unicode() >> 8));
            utf16.append(char(name.at(i).unicode() & 0xff));
            ++i;
        }
        QByteArray b64 = utf16.toBase64(QByteArray::OmitTrailingEquals);
        b64.replace('/', ',');
        out.append('&').append(b64).append('-');
    }
    return out;
}

const Value *fetchItem(const Value &list, const char *key)
{
    for (qsizetype i = 0; i + 1 < list.items.size(); i += 2) {
        const Value &name = list.items.at(i);
        if (name.kind == Value::Atom && name.text.compare(key, Qt::CaseInsensitive) == 0)
            return &list.items.at(i + 1);
    }
    return nullptr;
}

} // namespace Imap
