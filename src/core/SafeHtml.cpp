#include "SafeHtml.h"

#include <QRegularExpression>
#include <QSet>

namespace SafeHtml {

namespace {

// Elements taken out with what is inside them.
const QSet<QString> kDropped{QStringLiteral("script"), QStringLiteral("style"),    QStringLiteral("head"),
                             QStringLiteral("title"),  QStringLiteral("iframe"),   QStringLiteral("frame"),
                             QStringLiteral("frameset"), QStringLiteral("object"), QStringLiteral("embed"),
                             QStringLiteral("template"), QStringLiteral("svg"),    QStringLiteral("canvas"),
                             QStringLiteral("video"),  QStringLiteral("audio"),    QStringLiteral("noscript"),
                             QStringLiteral("math"),   QStringLiteral("applet")};
// Tags taken out, what is inside them kept.
const QSet<QString> kUnwrapped{QStringLiteral("form"),  QStringLiteral("input"),  QStringLiteral("button"),
                               QStringLiteral("select"), QStringLiteral("option"), QStringLiteral("textarea"),
                               QStringLiteral("link"),  QStringLiteral("meta"),   QStringLiteral("base"),
                               QStringLiteral("html"),  QStringLiteral("body"),   QStringLiteral("source"),
                               QStringLiteral("picture")};

QString escape(const QString &text)
{
    return text.toHtmlEscaped();
}

struct Tag
{
    QString name; // lowercase
    bool closing = false;
    QList<QPair<QString, QString>> attributes; // lowercase names
};

// The tag that starts at `from` ('<'); `end` is just after its '>'.
bool readTag(const QString &html, qsizetype from, Tag *tag, qsizetype *end)
{
    qsizetype i = from + 1;
    if (i < html.size() && html.at(i) == QLatin1Char('/')) {
        tag->closing = true;
        ++i;
    }
    const qsizetype nameStart = i;
    while (i < html.size() && (html.at(i).isLetterOrNumber() || html.at(i) == QLatin1Char('-') || html.at(i) == QLatin1Char(':')))
        ++i;
    if (i == nameStart)
        return false;
    tag->name = html.mid(nameStart, i - nameStart).toLower();
    while (i < html.size()) {
        while (i < html.size() && (html.at(i).isSpace() || html.at(i) == QLatin1Char('/')))
            ++i;
        if (i >= html.size())
            break;
        if (html.at(i) == QLatin1Char('>')) {
            *end = i + 1;
            return true;
        }
        const qsizetype a = i;
        while (i < html.size() && !html.at(i).isSpace() && html.at(i) != QLatin1Char('=') && html.at(i) != QLatin1Char('>'))
            ++i;
        const QString name = html.mid(a, i - a).toLower();
        while (i < html.size() && html.at(i).isSpace())
            ++i;
        QString value;
        if (i < html.size() && html.at(i) == QLatin1Char('=')) {
            ++i;
            while (i < html.size() && html.at(i).isSpace())
                ++i;
            if (i < html.size() && (html.at(i) == QLatin1Char('"') || html.at(i) == QLatin1Char('\''))) {
                const QChar q = html.at(i++);
                const qsizetype close = html.indexOf(q, i);
                if (close < 0)
                    return false;
                value = html.mid(i, close - i);
                i = close + 1;
            } else {
                const qsizetype v = i;
                while (i < html.size() && !html.at(i).isSpace() && html.at(i) != QLatin1Char('>'))
                    ++i;
                value = html.mid(v, i - v);
            }
        }
        if (!name.isEmpty())
            tag->attributes.append({name, value});
    }
    return false;
}

bool isRemote(const QString &url)
{
    const QString u = url.trimmed().toLower();
    return u.startsWith(QLatin1String("http:")) || u.startsWith(QLatin1String("https:")) || u.startsWith(QLatin1String("//"))
        || u.startsWith(QLatin1String("ftp:"));
}

bool isSafeLink(const QString &url)
{
    const QString u = url.trimmed().toLower();
    return u.startsWith(QLatin1String("http:")) || u.startsWith(QLatin1String("https:")) || u.startsWith(QLatin1String("mailto:"))
        || u.startsWith(QLatin1Char('#'));
}

QString attributeText(const QString &value)
{
    QString v = value;
    v.replace(QLatin1Char('&'), QLatin1String("&amp;")).replace(QLatin1Char('"'), QLatin1String("&quot;"));
    v.replace(QLatin1Char('<'), QLatin1String("&lt;")).replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return v;
}

} // namespace

Result clean(const QString &html)
{
    Result r;
    QString out;
    out.reserve(html.size());
    qsizetype i = 0;
    while (i < html.size()) {
        const qsizetype lt = html.indexOf(QLatin1Char('<'), i);
        if (lt < 0) {
            out += html.mid(i);
            break;
        }
        out += html.mid(i, lt - i);
        if (html.mid(lt, 4) == QLatin1String("<!--")) {
            const qsizetype close = html.indexOf(QLatin1String("-->"), lt + 4);
            i = close < 0 ? html.size() : close + 3;
            continue;
        }
        if (html.mid(lt, 2) == QLatin1String("<!") || html.mid(lt, 2) == QLatin1String("<?")) {
            const qsizetype close = html.indexOf(QLatin1Char('>'), lt);
            i = close < 0 ? html.size() : close + 1;
            continue;
        }
        Tag tag;
        qsizetype end = 0;
        if (!readTag(html, lt, &tag, &end)) {
            out += QStringLiteral("&lt;");
            i = lt + 1;
            continue;
        }
        i = end;
        if (kDropped.contains(tag.name)) {
            if (!tag.closing) { // to its end tag, or the end
                static const QString closeFormat = QStringLiteral("</%1");
                const qsizetype close = html.indexOf(closeFormat.arg(tag.name), i, Qt::CaseInsensitive);
                if (close < 0) {
                    i = html.size();
                } else {
                    const qsizetype gt = html.indexOf(QLatin1Char('>'), close);
                    i = gt < 0 ? html.size() : gt + 1;
                }
            }
            continue;
        }
        if (kUnwrapped.contains(tag.name))
            continue;
        if (tag.closing) {
            out += QStringLiteral("</") + tag.name + QLatin1Char('>');
            continue;
        }
        if (tag.name == QLatin1String("img")) {
            QString src;
            QString alt;
            for (const auto &a : std::as_const(tag.attributes)) {
                if (a.first == QLatin1String("src"))
                    src = a.second.trimmed();
                else if (a.first == QLatin1String("alt"))
                    alt = a.second;
            }
            if (src.startsWith(QLatin1String("cid:"), Qt::CaseInsensitive)) {
                out += QStringLiteral("<img src=\"") + attributeText(src) + QStringLiteral("\">");
            } else {
                if (isRemote(src))
                    ++r.remotePictures;
                if (!alt.trimmed().isEmpty())
                    out += QStringLiteral("<i>[") + escape(alt.trimmed()) + QStringLiteral("]</i>");
            }
            continue;
        }
        QString kept = QLatin1Char('<') + tag.name;
        for (const auto &a : std::as_const(tag.attributes)) {
            if (a.first.startsWith(QLatin1String("on")) || a.first == QLatin1String("background") || a.first == QLatin1String("srcset")
                || a.first == QLatin1String("src"))
                continue;
            if (a.first == QLatin1String("href") && !isSafeLink(a.second))
                continue;
            if (a.first == QLatin1String("style") && a.second.contains(QLatin1String("url("), Qt::CaseInsensitive)) {
                if (a.second.contains(QLatin1String("http"), Qt::CaseInsensitive))
                    ++r.remotePictures;
                continue;
            }
            kept += QLatin1Char(' ') + a.first + QStringLiteral("=\"") + attributeText(a.second) + QLatin1Char('"');
        }
        out += kept + QLatin1Char('>');
    }
    r.html = out;
    return r;
}

QString fromPlainText(const QString &text)
{
    static const QRegularExpression links(QStringLiteral("(https?://[^\\s<>\"]+)"));
    QStringList lines;
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    for (const QString &line : normalized.split(QLatin1Char('\n'))) {
        QString html = line.toHtmlEscaped();
        html.replace(links, QStringLiteral("<a href=\"\\1\">\\1</a>"));
        if (line.startsWith(QLatin1Char('>')))
            html = QStringLiteral("<span style=\"color:#777777\">") + html + QStringLiteral("</span>");
        lines.append(html);
    }
    return QStringLiteral("<div style=\"white-space: pre-wrap\">") + lines.join(QStringLiteral("<br>")) + QStringLiteral("</div>");
}

} // namespace SafeHtml
