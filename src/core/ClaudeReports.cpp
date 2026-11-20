#include "ClaudeReports.h"

#include "Claude.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>

namespace ClaudeReports {

namespace {

// Text as it was, in a block of its own: a fence longer than any in it.
QString fenced(const QString &text, const QString &language = QStringLiteral("text"))
{
    QString fence = QStringLiteral("```");
    while (text.contains(fence))
        fence += QLatin1Char('`');
    return fence + language + QLatin1Char('\n') + text + (text.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n")) + fence + QLatin1Char('\n');
}

QString json(const QJsonValue &value)
{
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    return fenced(QString::fromUtf8(document.toJson(QJsonDocument::Indented)), QStringLiteral("json"));
}

// What was carried along as data (a picture, a document): what it was and
// how big, not the data.
QString attachment(const QJsonObject &block)
{
    const QJsonObject source = block.value(QLatin1String("source")).toObject();
    const QString kind = block.value(QLatin1String("type")).toString();
    QString what = source.value(QLatin1String("media_type")).toString();
    if (what.isEmpty())
        what = source.value(QLatin1String("type")).toString();
    const qsizetype encoded = source.value(QLatin1String("data")).toString().size();
    QString size;
    if (encoded > 0)
        size = QStringLiteral(", about %1 kB").arg(QLocale(QLocale::English).toString(qMax<qsizetype>(1, encoded * 3 / 4 / 1000)));
    return QStringLiteral("*[%1: %2%3; not copied here]*\n").arg(kind == QLatin1String("image") ? QStringLiteral("a picture") : QStringLiteral("a document"), what, size);
}

// A message's content: a text, or blocks of several kinds.
QString contentOf(const QJsonValue &content)
{
    if (content.isString())
        return fenced(content.toString());
    QString out;
    for (const QJsonValue &value : content.toArray()) {
        const QJsonObject block = value.toObject();
        const QString type = block.value(QLatin1String("type")).toString();
        if (type == QLatin1String("text")) {
            out += fenced(block.value(QLatin1String("text")).toString());
        } else if (type == QLatin1String("image") || type == QLatin1String("document")) {
            out += attachment(block);
        } else if (type == QLatin1String("tool_use") || type == QLatin1String("server_tool_use")) {
            out += QStringLiteral("**Tool call: `%1`**\n\n").arg(block.value(QLatin1String("name")).toString()) + json(block.value(QLatin1String("input")));
        } else if (type == QLatin1String("tool_result")) {
            out += QStringLiteral("**Tool result**%1\n\n").arg(block.value(QLatin1String("is_error")).toBool() ? QStringLiteral(" (an error)") : QString())
                   + contentOf(block.value(QLatin1String("content")));
        } else if (type == QLatin1String("thinking") || type == QLatin1String("redacted_thinking")) {
            const QString thought = block.value(QLatin1String("thinking")).toString();
            out += thought.isEmpty() ? QStringLiteral("*[Claude's thinking: not shown by the API]*\n") : QStringLiteral("*Claude's thinking (a summary):*\n\n") + fenced(thought);
        } else {
            out += QStringLiteral("*[%1]*\n").arg(type.isEmpty() ? QStringLiteral("a block of an unknown kind") : type);
        }
        out += QLatin1Char('\n');
    }
    return out.isEmpty() ? QStringLiteral("*(nothing)*\n") : out;
}

} // namespace

QString folder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/claudeReports");
}

QString fileName(const QString &askedBy, const QDateTime &when)
{
    // "Running Services" → "running_services": letters and digits, the rest one underline.
    QString who;
    for (const QChar c : askedBy.toLower()) {
        if (c.isLetterOrNumber())
            who += c;
        else if (!who.isEmpty() && !who.endsWith(QLatin1Char('_')))
            who += QLatin1Char('_');
    }
    while (who.endsWith(QLatin1Char('_')))
        who.chop(1);
    if (who.isEmpty())
        who = QStringLiteral("argonaut_emails");
    return who.left(60) + QStringLiteral("_report_") + when.toString(QStringLiteral("dd.MM.yy_HH.mm.ss")) + QStringLiteral(".md");
}

QString markdown(const QString &askedBy, const QJsonObject &request, const Claude::Response &response, const QDateTime &when)
{
    const QLocale english(QLocale::English);
    const QString who = askedBy.isEmpty() ? QStringLiteral("Argonaut Emails") : askedBy;
    QString out = QStringLiteral("# %1: Claude's report\n\n").arg(who);
    out += QStringLiteral("- **When:** %1\n").arg(english.toString(when, QStringLiteral("dddd d MMMM yyyy, HH:mm:ss")));
    out += QStringLiteral("- **Asked by:** %1\n").arg(who);
    out += QStringLiteral("- **Model:** %1\n").arg(response.message.value(QLatin1String("model")).toString(request.value(QLatin1String("model")).toString()));
    out += QStringLiteral("- **Tokens:** %1 sent, %2 received\n").arg(english.toString(response.inputTokens), english.toString(response.outputTokens));
    out += QStringLiteral("- **Cost:** about $%1\n").arg(QString::number(response.dollars, 'f', 4));
    if (!response.stopReason.isEmpty() && response.stopReason != QLatin1String("end_turn"))
        out += QStringLiteral("- **It stopped because:** %1\n").arg(response.stopReason);

    // The answer, as Claude wrote it: its text is Markdown already. What it
    // asked for (a tool) is shown as what it is.
    out += QStringLiteral("\n## Claude's answer\n\n");
    bool any = false;
    for (const QJsonValue &value : response.message.value(QLatin1String("content")).toArray()) {
        const QJsonObject block = value.toObject();
        const QString type = block.value(QLatin1String("type")).toString();
        if (type == QLatin1String("text")) {
            out += block.value(QLatin1String("text")).toString().trimmed() + QStringLiteral("\n\n");
            any = true;
        } else if (type == QLatin1String("tool_use") || type == QLatin1String("server_tool_use")) {
            out += QStringLiteral("**Tool call: `%1`**\n\n").arg(block.value(QLatin1String("name")).toString()) + json(block.value(QLatin1String("input")))
                   + QLatin1Char('\n');
            any = true;
        }
    }
    if (!any)
        out += QStringLiteral("*(no text)*\n\n");

    out += QStringLiteral("---\n\n## What was sent to Claude\n\n");
    out += QStringLiteral("### System instructions\n\n");
    const QJsonValue system = request.value(QLatin1String("system"));
    out += system.isUndefined() || (system.isString() && system.toString().isEmpty()) ? QStringLiteral("*(none)*\n") : contentOf(system);
    out += QStringLiteral("\n### The conversation\n\n");
    int number = 0;
    for (const QJsonValue &value : request.value(QLatin1String("messages")).toArray()) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QLatin1String("role")).toString();
        out += QStringLiteral("#### %1. %2\n\n")
                   .arg(++number)
                   .arg(role == QLatin1String("user") ? QStringLiteral("From the user (and the program)") : role == QLatin1String("assistant")
                                                                                                              ? QStringLiteral("Claude, earlier in this conversation")
                                                                                                              : role);
        out += contentOf(message.value(QLatin1String("content"))) + QLatin1Char('\n');
    }
    const QJsonArray tools = request.value(QLatin1String("tools")).toArray();
    if (!tools.isEmpty())
        out += QStringLiteral("### Tools Claude could use\n\n") + json(tools) + QLatin1Char('\n');
    // Everything else in the request: how much it may write, how it thinks.
    QJsonObject settings = request;
    for (const char *key : {"system", "messages", "tools"})
        settings.remove(QLatin1String(key));
    out += QStringLiteral("### Settings\n\n") + json(settings);
    return out;
}

QString save(const QString &askedBy, const QJsonObject &request, const Claude::Response &response, const QDateTime &when)
{
    const QString dir = folder();
    if (!QDir().mkpath(dir))
        return {};
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner); // what was sent can be private
    // Two answers in one second (a conversation of several turns): the later ones are numbered.
    const QString name = fileName(askedBy, when);
    QString path = dir + QLatin1Char('/') + name;
    for (int n = 2; QFile::exists(path) && n < 1000; ++n)
        path = dir + QLatin1Char('/') + QString(name).replace(QStringLiteral(".md"), QStringLiteral("_%1.md").arg(n));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(markdown(askedBy, request, response, when).toUtf8());
    if (!file.commit())
        return {};
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return path;
}

} // namespace ClaudeReports
