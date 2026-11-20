#include "Extensions.h"

#include "MailReader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace Extensions {

QString paletteName(const QString &label)
{
    return QStringLiteral("ext::") + label;
}

namespace {

// Limits on what an extension may send: it is someone else's program.
constexpr int MaxLineBytes = 8 * 1024 * 1024;
constexpr int MaxCommands = 20;
constexpr int MaxLabelChars = 60;
constexpr int MaxTextChars = 300;
constexpr int MaxMessageChars = 200;
constexpr int MaxResultChars = 1024 * 1024;
constexpr int MaxFormFields = 20;
constexpr int MaxClaudePerAction = 12;      // questions to Claude for one thing the user chose
constexpr int MaxClaudeTokens = 16000;      // the most an extension may ask Claude to write in one answer
constexpr int MaxMailPerAction = 6;         // questions about the mail for one thing the user chose
constexpr int ActingWithinMs = 5 * 60 * 1000; // what the user chose counts this long
constexpr int MaxFailures = 3;

// One line of plain text, cut to size.
QString tidy(const QString &text, int maxChars)
{
    QString out = text.left(maxChars);
    for (QChar &c : out) {
        if (c == QLatin1Char('\r') || c == QLatin1Char('\t') || (c.unicode() < 0x20 && c != QLatin1Char('\n')))
            c = QLatin1Char(' ');
    }
    return out.trimmed();
}

QString oneLine(const QString &text, int maxChars)
{
    return tidy(text, maxChars).replace(QLatin1Char('\n'), QLatin1Char(' '));
}

QString enabledKey(const QString &id)
{
    return QStringLiteral("extensions/%1/enabled").arg(id);
}

// Folders that hold extensions: (folder, came with the program).
QList<QPair<QString, bool>> searchFolders()
{
    QList<QPair<QString, bool>> folders;
    // Tests (and people trying an extension) name the folders themselves;
    // nothing else is looked at then. Those extensions start switched off.
    if (qEnvironmentVariableIsSet("MAIL_EXTENSIONS_PATH")) {
        const QStringList given = qEnvironmentVariable("MAIL_EXTENSIONS_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
        for (const QString &folder : given)
            folders.append({folder, false});
        return folders;
    }
    const QString user = userFolder();
    folders.append({user, false});
    // Next to the program (a build folder), then the system's data folders.
    folders.append({QCoreApplication::applicationDirPath() + QStringLiteral("/extensions"), true});
    const QStringList system = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("argonaut-emails/extensions"),
                                                         QStandardPaths::LocateDirectory);
    for (const QString &folder : system) {
        if (QDir::cleanPath(folder) != QDir::cleanPath(user))
            folders.append({folder, true});
    }
    return folders;
}

QList<Command> commandsFrom(const QString &extension, const QJsonValue &value)
{
    QList<Command> out;
    const QJsonArray list = value.toArray();
    for (const QJsonValue &entry : list) {
        if (out.size() >= MaxCommands)
            break;
        const QJsonObject o = entry.toObject();
        Command c;
        c.extension = extension;
        c.id = o.value(QLatin1String("id")).toString().left(64);
        c.label = oneLine(o.value(QLatin1String("label")).toString(), MaxLabelChars);
        if (c.id.isEmpty() || c.label.isEmpty())
            continue;
        c.confirm = tidy(o.value(QLatin1String("confirm")).toString(), MaxTextChars);
        QStringList group;
        const QStringList levels = o.value(QLatin1String("group")).toString().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString &level : levels.mid(0, 2)) {
            const QString name = oneLine(level, MaxLabelChars);
            if (!name.isEmpty())
                group << name;
        }
        c.group = group.join(QLatin1Char('/'));
        c.checked = o.value(QLatin1String("checked")).toBool();
        if (o.value(QLatin1String("ask")).isObject()) {
            const QJsonObject ask = o.value(QLatin1String("ask")).toObject();
            c.asks = true;
            c.askLabel = tidy(ask.value(QLatin1String("label")).toString(), MaxTextChars);
            c.askText = ask.value(QLatin1String("text")).toString().left(2000);
            c.askMultiline = ask.value(QLatin1String("multiline")).toBool();
        }
        if (o.contains(QLatin1String("menu"))) {
            // A menu we don't have: left out, not put somewhere it didn't ask for.
            c.menu = o.value(QLatin1String("menu")).toString();
            static const QStringList known{QStringLiteral("file"), QStringLiteral("edit"), QStringLiteral("view"), QStringLiteral("message")};
            if (!known.contains(c.menu))
                continue;
        }
        c.multiple = o.value(QLatin1String("multiple")).toBool(true);
        out.append(c);
    }
    return out;
}

} // namespace

QString userFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/extensions");
}

Manifest readManifest(const QString &folder, bool builtIn)
{
    Manifest m;
    m.folder = QDir::cleanPath(folder);
    m.builtIn = builtIn;
    m.id = QFileInfo(m.folder).fileName();
    m.name = m.id;
    QFile file(m.folder + QStringLiteral("/extension.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        m.problem = QCoreApplication::translate("Extensions", "It has no extension.json.");
        return m;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(256 * 1024), &error);
    if (!doc.isObject()) {
        m.problem = QCoreApplication::translate("Extensions", "Its extension.json can't be read: %1").arg(error.errorString());
        return m;
    }
    const QJsonObject o = doc.object();
    const QString id = o.value(QLatin1String("id")).toString();
    static const QRegularExpression goodId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    if (!goodId.match(id).hasMatch()) {
        m.problem = QCoreApplication::translate("Extensions", "Its \"id\" is missing or has characters other than letters, digits, '.', '-' and '_'.");
        return m;
    }
    m.id = id;
    m.name = oneLine(o.value(QLatin1String("name")).toString(id), 80);
    if (m.name.isEmpty())
        m.name = id;
    m.description = tidy(o.value(QLatin1String("description")).toString(), 600);
    m.version = oneLine(o.value(QLatin1String("version")).toString(), 40);
    m.author = oneLine(o.value(QLatin1String("author")).toString(), 80);
    const int protocol = o.value(QLatin1String("protocol")).toInt(0);
    if (protocol != ProtocolVersion) {
        m.problem = QCoreApplication::translate("Extensions", "It is made for another version of Argonaut Emails (protocol %1, this is %2).")
                        .arg(protocol)
                        .arg(ProtocolVersion);
        return m;
    }
    QStringList exec;
    const QJsonValue execValue = o.value(QLatin1String("exec"));
    if (execValue.isString()) {
        exec << execValue.toString();
    } else {
        const QJsonArray parts = execValue.toArray();
        for (const QJsonValue &part : parts)
            exec << part.toString();
    }
    if (exec.isEmpty() || exec.first().isEmpty()) {
        m.problem = QCoreApplication::translate("Extensions", "Its \"exec\" (the program to start) is missing.");
        return m;
    }
    QString program = exec.takeFirst();
    if (!QDir::isAbsolutePath(program))
        program = QDir::cleanPath(m.folder + QLatin1Char('/') + program);
    m.program = program;
    m.arguments = exec;
    const QJsonArray uses = o.value(QLatin1String("uses")).toArray();
    for (const QJsonValue &use : uses) {
        m.usesClaude = m.usesClaude || use.toString() == QLatin1String("claude");
        m.readsMail = m.readsMail || use.toString() == QLatin1String("mail");
    }
    m.whenNeeded = o.value(QLatin1String("start")).toString() == QLatin1String("when-needed");
    m.commands = commandsFrom(m.id, o.value(QLatin1String("commands")));
    const QFileInfo info(program);
    if (!info.isFile() || !info.isExecutable())
        m.problem = QCoreApplication::translate("Extensions", "Its program is missing or not executable: %1").arg(program);
    return m;
}

// ------------------------------------------------------------------ host

Host *Host::instance()
{
    // Never deleted: windows ask it while they are taken down, in any order.
    // Its programs are stopped when the application quits.
    static Host *host = [] {
        auto *h = new Host;
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, h, &Host::shutdown);
        return h;
    }();
    return host;
}

Host::Host(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<Extensions::Form>();
    qRegisterMetaType<Extensions::Result>();
    bool ok = false;
    const int idle = qEnvironmentVariableIntValue("MAIL_EXTENSION_IDLE_MS", &ok);
    if (ok && idle > 0)
        m_idleMs = idle;
    auto *idleTimer = new QTimer(this);
    idleTimer->setInterval(qBound(50, m_idleMs / 3, 20000));
    connect(idleTimer, &QTimer::timeout, this, &Host::stopIdle);
    idleTimer->start();
    rescan();
}

Host::~Host()
{
    for (Running *ext : std::as_const(m_running)) {
        stop(ext);
        delete ext;
    }
}

void Host::setMail(MailStore *store, std::function<Sync *(const QString &)> syncFor)
{
    delete m_mail;
    m_mail = store ? new MailReader(store, std::move(syncFor), this) : nullptr;
}

void Host::start()
{
    m_started = true;
    if (m_quitting)
        return;
    for (Running *ext : std::as_const(m_running)) {
        if (!ext->process && !ext->manifest.whenNeeded && ext->failures < MaxFailures)
            launch(ext);
    }
}

void Host::shutdown()
{
    m_quitting = true;
    for (Running *ext : std::as_const(m_running))
        stop(ext);
}

QList<Manifest> Host::extensions() const
{
    return m_manifests;
}

void Host::rescan()
{
    QList<Manifest> found;
    QSet<QString> ids;
    for (const auto &[folder, builtIn] : searchFolders()) {
        const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            Manifest m = readManifest(entry.absoluteFilePath(), builtIn);
            if (ids.contains(m.id))
                continue; // the first one found wins (the user's copy before ours)
            ids.insert(m.id);
            found.append(m);
        }
    }
    m_manifests = found;

    // An extension that is gone is no longer switched on: a folder with its
    // name that turns up later starts off.
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("extensions"));
        const QStringList known = settings.childGroups();
        for (const QString &id : known) {
            if (!ids.contains(id) && settings.value(id + QStringLiteral("/enabled")).toBool())
                settings.remove(id + QStringLiteral("/enabled"));
        }
    }

    QList<Running *> keep;
    for (Running *ext : std::as_const(m_running)) {
        bool still = false;
        for (const Manifest &m : std::as_const(m_manifests)) {
            if (m.id == ext->manifest.id && m.program == ext->manifest.program && m.arguments == ext->manifest.arguments && m.isUsable()
                && isEnabled(m.id)) {
                still = true;
                break;
            }
        }
        if (still) {
            keep.append(ext);
        } else {
            stop(ext);
            forget(ext);
            delete ext;
        }
    }
    m_running = keep;
    for (const Manifest &m : std::as_const(m_manifests)) {
        if (!m.isUsable() || !isEnabled(m.id))
            continue;
        bool have = false;
        for (Running *ext : std::as_const(m_running))
            have = have || ext->manifest.id == m.id;
        if (!have) {
            auto *ext = new Running;
            ext->manifest = m;
            m_running.append(ext);
        }
    }
    if (m_started)
        start();
    emit extensionsChanged();
}

bool Host::isEnabled(const QString &id) const
{
    bool builtIn = false;
    for (const Manifest &m : m_manifests) {
        if (m.id == id)
            builtIn = m.builtIn;
    }
    // What came with the program is on until switched off; anything else is off until switched on.
    return QSettings().value(enabledKey(id), builtIn).toBool();
}

void Host::setEnabled(const QString &id, bool on)
{
    QSettings().setValue(enabledKey(id), on);
    for (Running *ext : std::as_const(m_running)) {
        if (ext->manifest.id == id)
            ext->failures = 0; // switched on again: another try
    }
    rescan();
}

QList<Command> Host::commands(int count) const
{
    QList<Command> out;
    if (count <= 0)
        return out;
    for (const Running *ext : m_running) {
        if (!isAvailable(ext))
            continue;
        const QList<Command> all = commandsOf(ext);
        for (const Command &c : all) {
            if (c.menu.isEmpty() && (c.multiple || count == 1))
                out.append(c);
        }
    }
    return out;
}

QList<Command> Host::menuCommands(const QString &menu) const
{
    QList<Command> out;
    for (const Running *ext : m_running) {
        if (!isAvailable(ext))
            continue;
        const QList<Command> all = commandsOf(ext);
        for (const Command &c : all) {
            if (c.menu == menu)
                out.append(c);
        }
    }
    return out;
}

void Host::invoke(const Command &command, const QJsonArray &selection, const QString &input)
{
    for (Running *ext : std::as_const(m_running)) {
        if (ext->manifest.id != command.extension)
            continue;
        wake(ext); // a when-needed one starts now
        if (!ext->process)
            continue;
        ext->userActed = QDateTime::currentMSecsSinceEpoch();
        send(ext, {{QStringLiteral("type"), QStringLiteral("invoke")},
                   {QStringLiteral("command"), command.id},
                   {QStringLiteral("selection"), selection},
                   {QStringLiteral("input"), input}});
    }
}

QString Host::nameOf(const QString &id) const
{
    for (const Manifest &m : m_manifests) {
        if (m.id == id)
            return m.name;
    }
    return id;
}

Host::State Host::state(const QString &id) const
{
    for (const Manifest &m : m_manifests) {
        if (m.id == id && !m.isUsable())
            return State::Unusable;
    }
    for (const Running *ext : m_running) {
        if (ext->manifest.id != id)
            continue;
        if (ext->process && ext->process->state() == QProcess::Running)
            return State::Running;
        return ext->failures >= MaxFailures ? State::Stopped : State::Waiting;
    }
    return State::Off;
}

bool Host::isRunning(const QString &id) const
{
    const Running *ext = running(id);
    return ext && ext->process->state() == QProcess::Running;
}

Host::Running *Host::running(const QString &id) const
{
    for (Running *ext : m_running) {
        if (ext->manifest.id == id && ext->process)
            return ext;
    }
    return nullptr;
}

// ------------------------------------------------- started when needed ---

bool Host::isAvailable(const Running *ext) const
{
    return ext->process || (ext->manifest.whenNeeded && ext->failures < MaxFailures && !m_quitting);
}

QList<Command> Host::commandsOf(const Running *ext) const
{
    return ext->process && ext->commandsSaid ? ext->commands : ext->manifest.commands;
}

void Host::wake(Running *ext)
{
    ext->lastActive = QDateTime::currentMSecsSinceEpoch();
    if (!ext->process && ext->failures < MaxFailures && !m_quitting)
        launch(ext);
}

// A when-needed extension with nothing asked and nothing said for a while
// is stopped: it starts again with the next use.
void Host::stopIdle()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (Running *ext : std::as_const(m_running)) {
        if (!ext->process || !ext->manifest.whenNeeded)
            continue;
        if (!ext->formPending.isEmpty() || ext->claudePending > 0 || ext->mailPending > 0)
            continue;
        if (now - ext->lastActive < m_idleMs)
            continue;
        stop(ext);
        forget(ext);
    }
}

// ------------------------------------------------------------- programs ---

void Host::launch(Running *ext)
{
    auto *process = new QProcess(this);
    ext->process = process;
    ext->buffer.clear();
    ext->stopping = false;
    ext->lastActive = QDateTime::currentMSecsSinceEpoch();
    process->setWorkingDirectory(ext->manifest.folder);
    // Our environment, without what is only ours to know.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("ANTHROPIC_API_KEY"));
    env.insert(QStringLiteral("ARGONAUT_EMAILS_EXTENSION_ID"), ext->manifest.id);
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardErrorFile(QProcess::nullDevice());
    connect(process, &QProcess::readyReadStandardOutput, this, [this, ext, process] {
        if (ext->process == process)
            readFrom(ext);
    });
    connect(process, &QProcess::finished, this, [this, ext, process] {
        if (ext->process == process)
            exited(ext);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, ext, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && ext->process == process) {
            exited(ext);
            process->deleteLater();
        }
    });
    process->start(ext->manifest.program, ext->manifest.arguments);
    send(ext, {{QStringLiteral("type"), QStringLiteral("hello")},
               {QStringLiteral("protocol"), ProtocolVersion},
               {QStringLiteral("app"), QStringLiteral("argonaut-emails")},
               {QStringLiteral("version"), QCoreApplication::applicationVersion()}});
}

void Host::stop(Running *ext)
{
    if (!ext->process)
        return;
    ext->stopping = true;
    QProcess *process = ext->process;
    ext->process = nullptr;
    process->disconnect(this);
    process->closeWriteChannel(); // end of input: a well-behaved one leaves
    if (!process->waitForFinished(300)) {
        process->kill();
        process->waitForFinished(500);
    }
    process->deleteLater();
}

void Host::send(Running *ext, const QJsonObject &message)
{
    if (!ext->process)
        return;
    ext->process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void Host::readFrom(Running *ext)
{
    ext->buffer += ext->process->readAllStandardOutput();
    while (ext->process) {
        const qsizetype end = ext->buffer.indexOf('\n');
        if (end < 0)
            break;
        const QByteArray line = ext->buffer.left(end);
        ext->buffer.remove(0, end + 1);
        if (!line.trimmed().isEmpty())
            handle(ext, line);
    }
    if (ext->process && ext->buffer.size() > MaxLineBytes) {
        ext->failures = MaxFailures; // a line without end: not something to keep in memory
        stop(ext);
        forget(ext);
    }
}

void Host::handle(Running *ext, const QByteArray &line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return; // not ours to understand: skipped
    const QJsonObject message = doc.object();
    const QString type = message.value(QLatin1String("type")).toString();
    ext->lastActive = QDateTime::currentMSecsSinceEpoch();
    if (type == QLatin1String("commands")) {
        ext->commands = commandsFrom(ext->manifest.id, message.value(QLatin1String("commands")));
        ext->commandsSaid = true;
        emit extensionsChanged();
    } else if (type == QLatin1String("message")) {
        const QString text = oneLine(message.value(QLatin1String("text")).toString(), MaxMessageChars);
        if (!text.isEmpty())
            emit this->message(ext->manifest.name, text);
    } else if (type == QLatin1String("show")) {
        hearShow(ext, message);
    } else if (type == QLatin1String("form")) {
        hearForm(ext, message);
    } else if (type == QLatin1String("claude")) {
        askClaude(ext, message);
    } else if (type == QLatin1String("mail")) {
        readMail(ext, message);
    }
}

void Host::hearShow(Running *ext, const QJsonObject &message)
{
    Result result;
    const QString kind = message.value(QLatin1String("kind")).toString();
    result.kind = kind == QLatin1String("markdown") ? Result::Markdown : kind == QLatin1String("text") ? Result::Text : Result::None;
    if (result.kind == Result::None)
        return;
    result.extension = ext->manifest.id;
    result.title = oneLine(message.value(QLatin1String("title")).toString(), MaxLabelChars);
    result.text = message.value(QLatin1String("text")).toString().left(MaxResultChars);
    result.note = oneLine(message.value(QLatin1String("note")).toString(), MaxMessageChars);
    emit showRequested(result);
}

// ----------------------------------------------------------------- forms ---

void Host::hearForm(Running *ext, const QJsonObject &message)
{
    // Only as part of something the user chose, and one at a time.
    if (!isActingForUser(ext) || !ext->formPending.isEmpty())
        return;
    Form form;
    form.extension = ext->manifest.id;
    form.extensionName = ext->manifest.name;
    form.id = message.value(QLatin1String("id")).toString().left(64);
    if (form.id.isEmpty())
        return;
    form.title = oneLine(message.value(QLatin1String("title")).toString(), 80);
    form.heading = oneLine(message.value(QLatin1String("heading")).toString(), 120);
    form.text = tidy(message.value(QLatin1String("text")).toString(), 2000);
    form.warning = tidy(message.value(QLatin1String("warning")).toString(), MaxTextChars);
    const QJsonArray fields = message.value(QLatin1String("fields")).toArray();
    for (const QJsonValue &value : fields) {
        if (form.fields.size() >= MaxFormFields)
            break;
        const QJsonObject o = value.toObject();
        FormField f;
        f.id = o.value(QLatin1String("id")).toString().left(64);
        if (f.id.isEmpty())
            continue;
        const QString kind = o.value(QLatin1String("kind")).toString();
        f.kind = kind == QLatin1String("secret")  ? FormField::Secret
                 : kind == QLatin1String("check") ? FormField::Check
                 : kind == QLatin1String("choice") ? FormField::Choice
                                                   : FormField::Text;
        f.label = oneLine(o.value(QLatin1String("label")).toString(), 80);
        const QJsonValue given = o.value(QLatin1String("value"));
        f.value = given.isBool() ? (given.toBool() ? QStringLiteral("true") : QStringLiteral("false")) : given.toString().left(2000);
        f.placeholder = oneLine(o.value(QLatin1String("placeholder")).toString(), 120);
        const QJsonArray choices = o.value(QLatin1String("choices")).toArray();
        for (const QJsonValue &c : choices) {
            if (f.choices.size() < 50)
                f.choices << oneLine(c.toString(), 120);
        }
        if (f.kind == FormField::Choice && f.choices.isEmpty())
            continue;
        f.alone = o.value(QLatin1String("alone")).toBool();
        form.fields.append(f);
    }
    const QJsonArray buttons = message.value(QLatin1String("buttons")).toArray();
    for (const QJsonValue &b : buttons) {
        const QString label = oneLine(b.toString(), 40);
        if (!label.isEmpty() && form.buttons.size() < 3)
            form.buttons << label;
    }
    ext->formPending = form.id;
    ext->formTaken = false;
    emit formRequested(form); // a window takes it (queued: it shows a dialog) and answers
}

bool Host::takeForm(const QString &extension, const QString &id)
{
    for (Running *ext : std::as_const(m_running)) {
        if (ext->manifest.id == extension && ext->formPending == id && !ext->formTaken) {
            ext->formTaken = true;
            return true;
        }
    }
    return false;
}

void Host::answerForm(const QString &extension, const QString &id, bool cancelled, const QHash<QString, QString> &values, int button)
{
    for (Running *ext : std::as_const(m_running)) {
        if (ext->manifest.id != extension || ext->formPending != id)
            continue;
        ext->formPending.clear();
        ext->formTaken = false;
        QJsonObject answer{{QStringLiteral("type"), QStringLiteral("form")}, {QStringLiteral("id"), id}, {QStringLiteral("cancelled"), cancelled}};
        if (!cancelled) {
            // An answered form is the user choosing again: its work goes on.
            ext->userActed = QDateTime::currentMSecsSinceEpoch();
            QJsonObject fields;
            for (auto it = values.constBegin(); it != values.constEnd(); ++it)
                fields.insert(it.key(), it.value());
            answer.insert(QStringLiteral("values"), fields);
            answer.insert(QStringLiteral("button"), button);
        }
        send(ext, answer);
    }
}

// ---------------------------------------------------------------- Claude ---

bool Host::isActingForUser(const Running *ext)
{
    return ext->userActed != 0 && QDateTime::currentMSecsSinceEpoch() - ext->userActed <= ActingWithinMs;
}

void Host::stopWaitingForClaude(const QString &extension)
{
    for (Running *ext : std::as_const(m_running)) {
        if (ext->manifest.id != extension || ext->claudeWaiting.isEmpty())
            continue;
        const QStringList ids = ext->claudeWaiting;
        ext->claudeWaiting.clear();
        for (const QString &id : ids) {
            send(ext, {{QStringLiteral("type"), QStringLiteral("claude")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), QStringLiteral("Stopped: you chose not to wait for Claude")}}}});
        }
        emit claudeWaiting(extension, ext->manifest.name, 0);
    }
}

double Host::claudeSpent(const QString &extension)
{
    return QSettings().value(QStringLiteral("extensions/%1/claudeSpent").arg(extension)).toDouble();
}

// What an extension asks Claude is its own; which model answers, how much
// it may write and how the request is carried are the user's and ours.
static void keepToWhatIsTheExtensions(QJsonObject *request)
{
    for (const char *key : {"model", "stream", "fallbacks", "service_tier", "metadata", "container", "mcp_servers"})
        request->remove(QLatin1String(key));
    if (request->contains(QLatin1String("max_tokens")))
        request->insert(QStringLiteral("max_tokens"), qBound(1, request->value(QLatin1String("max_tokens")).toInt(), MaxClaudeTokens));
}

// An extension never has the user's API key. It sends the request it wants
// made ({"type":"claude","id":…,"request":{…}}) and the program makes it:
//   "ask": "info"   whether there is a key, and the model chosen (free)
//   "ask": "count"  how many tokens the request is (free), and what they would cost
//   else            the request is sent; the answer comes back whole
// Only for an extension that said "uses": ["claude"], and (but for "info")
// only while it does what the user chose, a few questions an action.
void Host::askClaude(Running *ext, const QJsonObject &message)
{
    const QString id = message.value(QLatin1String("id")).toString().left(64);
    const QString extension = ext->manifest.id;
    auto refuse = [&](const QString &why) {
        send(ext, {{QStringLiteral("type"), QStringLiteral("claude")},
                   {QStringLiteral("id"), id},
                   {QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), why}}}});
    };
    if (!ext->manifest.usesClaude) {
        refuse(QStringLiteral("This extension hasn't said that it uses Claude (\"uses\": [\"claude\"] in its extension.json)"));
        return;
    }
    if (!m_claude)
        m_claude = new Claude::Client(this);
    const QString ask = message.value(QLatin1String("ask")).toString();
    if (ask == QLatin1String("info")) {
        const Claude::ModelInfo model = Claude::modelInfo(Claude::model());
        send(ext, {{QStringLiteral("type"), QStringLiteral("claude")},
                   {QStringLiteral("id"), id},
                   {QStringLiteral("hasKey"), !m_claude->apiKey().isEmpty()},
                   {QStringLiteral("model"), model.id},
                   {QStringLiteral("modelName"), model.name}});
        return;
    }
    if (!isActingForUser(ext)) {
        refuse(QStringLiteral("Claude is asked only while the extension is doing what the user chose"));
        return;
    }
    if (ext->claudeFor != ext->userActed) { // another action of the user's: its own count
        ext->claudeFor = ext->userActed;
        ext->claudeAsked = 0;
    }
    if (m_claude->apiKey().isEmpty()) {
        refuse(QStringLiteral("There is no Claude API key yet: Edit → Claude"));
        return;
    }
    QJsonObject request = message.value(QLatin1String("request")).toObject();
    if (!request.value(QLatin1String("messages")).isArray()) {
        refuse(QStringLiteral("No request (\"request\": {\"messages\": […]})"));
        return;
    }
    keepToWhatIsTheExtensions(&request);
    if (ask == QLatin1String("count")) {
        request.remove(QLatin1String("max_tokens"));
        request.insert(QStringLiteral("model"), Claude::model());
        ++ext->claudePending;
        m_claude->countTokens(request, [this, extension, id](qint64 tokens, const Claude::Error &error) {
            Running *asker = running(extension);
            if (!asker)
                return;
            asker->claudePending = qMax(0, asker->claudePending - 1);
            QJsonObject answer{{QStringLiteral("type"), QStringLiteral("claude")}, {QStringLiteral("id"), id}};
            if (!error.message.isEmpty()) {
                answer.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), error.message}, {QStringLiteral("status"), error.status}});
            } else {
                answer.insert(QStringLiteral("tokens"), double(tokens));
                answer.insert(QStringLiteral("dollars"), Claude::cost(Claude::model(), tokens, 0));
            }
            send(asker, answer);
        });
        return;
    }
    if (ext->claudeAsked >= MaxClaudePerAction) {
        refuse(QStringLiteral("That is many questions for one thing the user chose: no more until they choose again"));
        return;
    }
    ++ext->claudeAsked;
    ++ext->claudePending;
    ext->claudeWaiting << id;
    emit claudeWaiting(extension, ext->manifest.name, int(ext->claudeWaiting.size()));
    m_claude->setAskedBy(ext->manifest.name); // the answer is kept as a report under its name
    m_claude->send(request, [this, id, extension](const Claude::Response &response, const Claude::Error &error) {
        if (response.dollars > 0) { // paid, whether or not the extension is still there to hear the answer
            QSettings settings;
            const QString key = QStringLiteral("extensions/%1/claudeSpent").arg(extension);
            settings.setValue(key, settings.value(key).toDouble() + response.dollars);
        }
        Running *asker = running(extension);
        if (!asker)
            return;
        asker->claudePending = qMax(0, asker->claudePending - 1);
        asker->lastActive = QDateTime::currentMSecsSinceEpoch();
        if (!asker->claudeWaiting.removeOne(id))
            return; // the user stopped waiting for this one
        emit claudeWaiting(extension, asker->manifest.name, int(asker->claudeWaiting.size()));
        QJsonObject answer{{QStringLiteral("type"), QStringLiteral("claude")}, {QStringLiteral("id"), id}};
        if (!error.message.isEmpty()) {
            answer.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), error.message}, {QStringLiteral("status"), error.status}});
        } else {
            answer.insert(QStringLiteral("response"), response.message);
            answer.insert(QStringLiteral("text"), response.text);
            if (!response.reportFile.isEmpty())
                answer.insert(QStringLiteral("report_file"), response.reportFile);
            answer.insert(QStringLiteral("stop_reason"), response.stopReason);
            answer.insert(QStringLiteral("dollars"), response.dollars);
        }
        send(asker, answer);
    });
}

// ------------------------------------------------------------------ mail ---

// {"type":"mail","id":…,"ask":"messages","folder":"inbox","days":7,"limit":300,"text":true,"chars":1500}
// The messages of that kind of folder of every account, newest first (MailReader).
// Only for an extension that said "uses": ["mail"], only while it does what
// the user chose, and a few questions an action.
void Host::readMail(Running *ext, const QJsonObject &message)
{
    const QString id = message.value(QLatin1String("id")).toString().left(64);
    const QString extension = ext->manifest.id;
    auto refuse = [&](const QString &why) {
        send(ext, {{QStringLiteral("type"), QStringLiteral("mail")},
                   {QStringLiteral("id"), id},
                   {QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), why}}}});
    };
    if (!ext->manifest.readsMail) {
        refuse(QStringLiteral("This extension hasn't said that it reads mail (\"uses\": [\"mail\"] in its extension.json)"));
        return;
    }
    if (!isActingForUser(ext)) {
        refuse(QStringLiteral("Mail is read only while the extension is doing what the user chose"));
        return;
    }
    if (ext->mailFor != ext->userActed) {
        ext->mailFor = ext->userActed;
        ext->mailAsked = 0;
    }
    if (ext->mailAsked >= MaxMailPerAction) {
        refuse(QStringLiteral("That is many questions for one thing the user chose"));
        return;
    }
    if (!m_mail) {
        refuse(QStringLiteral("There is no mail here"));
        return;
    }
    if (message.value(QLatin1String("ask")).toString(QStringLiteral("messages")) != QLatin1String("messages")) {
        refuse(QStringLiteral("Unknown question"));
        return;
    }
    ++ext->mailAsked;
    MailReader::Query query;
    static const QStringList roles{QStringLiteral("inbox"),  QStringLiteral("all"),   QStringLiteral("starred"), QStringLiteral("sent"),
                                   QStringLiteral("drafts"), QStringLiteral("archive"), QStringLiteral("junk"),  QStringLiteral("trash")};
    query.role = message.value(QLatin1String("folder")).toString(QStringLiteral("inbox"));
    if (!roles.contains(query.role)) {
        refuse(QStringLiteral("Unknown folder: one of %1").arg(roles.join(QStringLiteral(", "))));
        return;
    }
    const int days = qBound(1, message.value(QLatin1String("days")).toInt(7), MaxMailDays);
    query.since = QDateTime::currentDateTime().addDays(-days);
    query.limit = qBound(1, message.value(QLatin1String("limit")).toInt(300), MaxMailMessages);
    query.text = message.value(QLatin1String("text")).toBool();
    query.chars = qBound(50, message.value(QLatin1String("chars")).toInt(1500), MaxMailChars);
    ++ext->mailPending;
    m_mail->read(query, [this, extension, id, query](const QJsonArray &messages, const QString &error) {
        Running *asker = running(extension);
        if (!asker)
            return;
        asker->mailPending = qMax(0, asker->mailPending - 1);
        asker->lastActive = QDateTime::currentMSecsSinceEpoch();
        QJsonObject answer{{QStringLiteral("type"), QStringLiteral("mail")},
                           {QStringLiteral("id"), id},
                           {QStringLiteral("since"), query.since.toString(Qt::ISODate)},
                           {QStringLiteral("messages"), messages}};
        if (!error.isEmpty()) // some texts couldn't be read: the rest is still the answer
            answer.insert(QStringLiteral("warning"), error);
        send(asker, answer);
    });
}

// ---------------------------------------------------------------- endings ---

void Host::exited(Running *ext)
{
    ext->process = nullptr;
    ext->buffer.clear();
    forget(ext);
    if (ext->stopping)
        return;
    ++ext->failures; // a crash, or it couldn't start: a few more tries, when next needed
    if (ext->failures < MaxFailures && !ext->manifest.whenNeeded) {
        const QString id = ext->manifest.id;
        QTimer::singleShot(1000 * ext->failures, this, [this, id] {
            for (Running *r : std::as_const(m_running)) {
                if (r->manifest.id == id && !r->process && m_started && !m_quitting)
                    launch(r);
            }
        });
    }
}

void Host::forget(Running *ext)
{
    ext->userActed = 0;
    ext->formPending.clear();
    ext->formTaken = false;
    ext->claudePending = 0;
    ext->mailPending = 0;
    if (!ext->claudeWaiting.isEmpty()) {
        ext->claudeWaiting.clear();
        emit claudeWaiting(ext->manifest.id, ext->manifest.name, 0);
    }
    ext->commands.clear();
    ext->commandsSaid = false;
}

} // namespace Extensions
