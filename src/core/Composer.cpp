#include "Composer.h"

#include <QLocale>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUuid>

namespace Composer {

namespace {

// No line breaks from what the user typed: they would start new headers.
QString oneLine(QString value)
{
    for (QChar &c : value) {
        if (c == QLatin1Char('\r') || c == QLatin1Char('\n') || c.unicode() == 0)
            c = QLatin1Char(' ');
    }
    return value;
}

bool isPlainAscii(const QString &value)
{
    for (const QChar c : value) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e)
            return false;
    }
    return !value.contains(QLatin1String("=?"));
}

// Encoded words of at most 75 characters, never cutting a letter in two.
QList<QByteArray> encodedWords(const QString &value)
{
    QList<QByteArray> words;
    QByteArray chunk;
    const QByteArray prefix = "=?UTF-8?B?";
    for (qsizetype i = 0; i < value.size(); ++i) {
        QString one(value.at(i));
        if (value.at(i).isHighSurrogate() && i + 1 < value.size())
            one.append(value.at(++i));
        const QByteArray bytes = one.toUtf8();
        // At most 66 characters a word: "Subject: " and a word still fit in 78.
        if (((chunk.size() + bytes.size() + 2) / 3) * 4 + prefix.size() + 2 > 66) {
            words.append(prefix + chunk.toBase64() + "?=");
            chunk.clear();
        }
        chunk += bytes;
    }
    if (!chunk.isEmpty())
        words.append(prefix + chunk.toBase64() + "?=");
    return words;
}

QByteArray foldWords(const QList<QByteArray> &words)
{
    return words.join("\r\n ");
}

bool needsQuotes(const QString &name)
{
    static const QString specials = QStringLiteral("()<>@,;:\\\".[]");
    for (const QChar c : name) {
        if (specials.contains(c))
            return true;
    }
    return false;
}

bool isSafeEmail(const QString &email)
{
    if (email.isEmpty() || !email.contains(QLatin1Char('@')))
        return false;
    for (const QChar c : email) {
        if (c.unicode() <= 0x20 || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char(',') || c == QLatin1Char('"'))
            return false;
    }
    return true;
}

QByteArray base64Lines(const QByteArray &data)
{
    const QByteArray b64 = data.toBase64();
    QByteArray out;
    out.reserve(b64.size() + b64.size() / 76 * 2 + 2);
    for (qsizetype i = 0; i < b64.size(); i += 76)
        out.append(b64.mid(i, 76)).append("\r\n");
    return out;
}

QByteArray fileParams(const QString &name, const char *key)
{
    const QString clean = oneLine(name).remove(QLatin1Char('"')).remove(QLatin1Char('\\'));
    if (isPlainAscii(clean))
        return QByteArray(key) + "=\"" + clean.toLatin1() + '"';
    // RFC 2231 for the ones who know it, in pieces of a line each.
    const QByteArray encoded = "UTF-8''" + clean.toUtf8().toPercentEncoding();
    QList<QByteArray> pieces;
    qsizetype i = 0;
    while (i < encoded.size()) {
        qsizetype n = qMin<qsizetype>(60, encoded.size() - i);
        // Never cut a %XX in two.
        if (i + n < encoded.size()) {
            if (encoded.at(i + n - 1) == '%')
                n -= 1;
            else if (n >= 2 && encoded.at(i + n - 2) == '%')
                n -= 2;
        }
        pieces.append(encoded.mid(i, n));
        i += n;
    }
    if (pieces.size() == 1)
        return QByteArray(key) + "*=" + pieces.first();
    QList<QByteArray> parts;
    for (int k = 0; k < pieces.size(); ++k)
        parts.append(QByteArray(key) + '*' + QByteArray::number(k) + "*=" + pieces.at(k));
    return parts.join(";\r\n ");
}

QString quoteLines(const QString &text)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n')))
        out.append(line.startsWith(QLatin1Char('>')) ? QLatin1Char('>') + line : QStringLiteral("> ") + line);
    return out.join(QLatin1Char('\n'));
}

// The text of a message to quote: its plain text, or the HTML's words.
QString readableText(const Mime::Message &message)
{
    QString text = message.plainText();
    if (text.isEmpty()) {
        static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
        static const QRegularExpression styles(QStringLiteral("<(style|script|head)[^>]*>.*?</\\1>"),
                                               QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        QString html = message.html();
        html.remove(styles);
        html.replace(QRegularExpression(QStringLiteral("<br\\s*/?>|</p>|</div>"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
        html.remove(tags);
        html.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
        html.replace(QLatin1String("&lt;"), QLatin1String("<"));
        html.replace(QLatin1String("&gt;"), QLatin1String(">"));
        html.replace(QLatin1String("&amp;"), QLatin1String("&"));
        text = html;
    }
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    return text.trimmed();
}

QString senderOf(const Mime::Message &message)
{
    const QList<Mime::Address> from = message.from();
    if (from.isEmpty())
        return QObject::tr("someone");
    return from.first().name.isEmpty() ? from.first().email : from.first().name;
}

} // namespace

QByteArray encodeHeader(const QString &value)
{
    const QString line = oneLine(value);
    if (isPlainAscii(line))
        return line.toLatin1();
    return foldWords(encodedWords(line));
}

QByteArray encodeAddresses(const QList<Mime::Address> &addresses)
{
    QList<QByteArray> out;
    for (const Mime::Address &a : addresses) {
        const QString email = oneLine(a.email).trimmed();
        if (!isSafeEmail(email))
            continue;
        const QString name = oneLine(a.name).trimmed();
        if (name.isEmpty()) {
            out.append(email.toUtf8());
            continue;
        }
        QByteArray encodedName;
        if (isPlainAscii(name)) {
            if (needsQuotes(name)) {
                QString escaped = name;
                escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
                encodedName = '"' + escaped.toLatin1() + '"';
            } else {
                encodedName = name.toLatin1();
            }
        } else {
            encodedName = foldWords(encodedWords(name));
        }
        // An encoded name is long: the address goes on a line of its own.
        out.append(encodedName + (isPlainAscii(name) ? " <" : "\r\n <") + email.toUtf8() + '>');
    }
    return out.join(",\r\n ");
}

QStringList recipients(const Draft &draft)
{
    QStringList out;
    for (const QList<Mime::Address> *list : {&draft.to, &draft.cc, &draft.bcc}) {
        for (const Mime::Address &a : *list) {
            const QString email = a.email.trimmed();
            if (isSafeEmail(email) && !out.contains(email, Qt::CaseInsensitive))
                out.append(email);
        }
    }
    return out;
}

QByteArray build(const Draft &draft)
{
    QByteArray m;
    const QDateTime date = draft.date.isValid() ? draft.date : QDateTime::currentDateTime();
    m += "Date: " + QLocale::c().toString(date, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss ")).toLatin1();
    const int offset = date.offsetFromUtc();
    m += QString::asprintf("%c%02d%02d\r\n", offset < 0 ? '-' : '+', qAbs(offset) / 3600, (qAbs(offset) % 3600) / 60).toLatin1();
    m += "From: " + encodeAddresses({draft.from}) + "\r\n";
    if (!draft.to.isEmpty())
        m += "To: " + encodeAddresses(draft.to) + "\r\n";
    if (!draft.cc.isEmpty())
        m += "Cc: " + encodeAddresses(draft.cc) + "\r\n";
    m += "Subject: " + encodeHeader(draft.subject) + "\r\n";
    QByteArray id = draft.messageId;
    if (id.isEmpty()) {
        QString domain = draft.from.email.section(QLatin1Char('@'), 1);
        if (domain.isEmpty() || !isPlainAscii(domain))
            domain = QStringLiteral("localhost");
        id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1() + '@' + domain.toLatin1();
    }
    m += "Message-ID: <" + id + ">\r\n";
    if (!draft.inReplyTo.isEmpty()) {
        m += "In-Reply-To: <" + oneLine(QString::fromLatin1(draft.inReplyTo)).toLatin1() + ">\r\n";
        QByteArray refs = oneLine(QString::fromLatin1(draft.references)).toLatin1().simplified();
        if (!refs.contains(draft.inReplyTo))
            refs = (refs + " <" + draft.inReplyTo + '>').trimmed();
        m += "References: " + refs.replace(" <", "\r\n <") + "\r\n";
    }
    m += "MIME-Version: 1.0\r\n";
    m += "User-Agent: Argonaut Emails " MAIL_VERSION "\r\n";

    QString text = draft.text;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    const QByteArray textPart = "Content-Type: text/plain; charset=UTF-8\r\n"
                                "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
        + Mime::encodeQuotedPrintable(text.toUtf8());
    if (draft.files.isEmpty()) {
        m += textPart;
        return m;
    }
    const QByteArray boundary = "=_argonaut_" + QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    m += "Content-Type: multipart/mixed;\r\n boundary=\"" + boundary + "\"\r\n\r\n";
    m += "This is a message in several parts (MIME).\r\n";
    // The line break before a delimiter is the delimiter's: one more, so the text keeps its own.
    m += "--" + boundary + "\r\n" + textPart + "\r\n";
    QMimeDatabase types;
    for (const File &f : draft.files) {
        QByteArray type = f.type.isEmpty() ? types.mimeTypeForFile(f.name, QMimeDatabase::MatchExtension).name().toLatin1() : f.type;
        if (type.isEmpty() || type.contains('\r') || type.contains('\n'))
            type = "application/octet-stream";
        m += "--" + boundary + "\r\n";
        m += "Content-Type: " + type + ";\r\n " + fileParams(f.name, "name") + "\r\n";
        m += "Content-Disposition: attachment;\r\n " + fileParams(f.name, "filename") + "\r\n";
        m += "Content-Transfer-Encoding: base64\r\n\r\n";
        m += base64Lines(f.data);
    }
    m += "--" + boundary + "--\r\n";
    return m;
}

QString replySubject(const QString &subject)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(re|aw|sv|απ|ΑΠ)\\s*:"), QRegularExpression::CaseInsensitiveOption);
    if (re.match(subject).hasMatch())
        return subject;
    return QStringLiteral("Re: ") + subject;
}

QString forwardSubject(const QString &subject)
{
    static const QRegularExpression fwd(QStringLiteral("^\\s*(fwd?|προώθ)\\s*:"), QRegularExpression::CaseInsensitiveOption);
    if (fwd.match(subject).hasMatch())
        return subject;
    return QStringLiteral("Fwd: ") + subject;
}

QString quoted(const Mime::Message &message)
{
    const QDateTime date = message.date();
    const QString when = date.isValid() ? QLocale().toString(date.toLocalTime(), QLocale::LongFormat) : QString();
    const QString intro = when.isEmpty() ? QObject::tr("%1 wrote:").arg(senderOf(message))
                                         : QObject::tr("On %1, %2 wrote:").arg(when, senderOf(message));
    return QStringLiteral("\n\n") + intro + QLatin1Char('\n') + quoteLines(readableText(message)) + QLatin1Char('\n');
}

QString forwardedText(const Mime::Message &message)
{
    QStringList to;
    for (const Mime::Address &a : message.to())
        to.append(a.display());
    QStringList lines;
    lines << QString() << QString() << QObject::tr("-------- Forwarded Message --------");
    lines << QObject::tr("Subject: %1").arg(message.subject());
    const QDateTime date = message.date();
    if (date.isValid())
        lines << QObject::tr("Date: %1").arg(QLocale().toString(date.toLocalTime(), QLocale::LongFormat));
    if (!message.from().isEmpty())
        lines << QObject::tr("From: %1").arg(message.from().first().display());
    if (!to.isEmpty())
        lines << QObject::tr("To: %1").arg(to.join(QStringLiteral(", ")));
    lines << QString() << readableText(message) << QString();
    return lines.join(QLatin1Char('\n'));
}

} // namespace Composer
