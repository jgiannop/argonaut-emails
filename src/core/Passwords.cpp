#include "Passwords.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#if defined(MAIL_DBUS)
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QEventLoop>
#include <QTimer>
#endif

namespace {

// The one being worked on (the functions below were written for one key).
QString g_account;
QString g_kind;
QString g_label;

QString keyFile()
{
    // Ids are made by us (QUuid), kinds are "imap"/"smtp": no path tricks.
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/passwords/")
        + g_account + QLatin1Char('-') + g_kind;
}

bool forceFile()
{
    return qEnvironmentVariable("MAIL_KEYSTORE") == QLatin1String("file");
}

QString readFile()
{
    QFile file(keyFile());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

bool writeFile(const QString &key, QString *error)
{
    QDir().mkpath(QFileInfo(keyFile()).absolutePath());
    QFile::setPermissions(QFileInfo(keyFile()).absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile file(keyFile());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner); // before anything is written
    file.write(key.toUtf8());
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

#if defined(MAIL_DBUS)
// ------------------------------------------------- freedesktop Secret Service

const QString kService = QStringLiteral("org.freedesktop.secrets");
const QString kPath = QStringLiteral("/org/freedesktop/secrets");
const QString kServiceIface = QStringLiteral("org.freedesktop.Secret.Service");

struct Secret
{
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &arg, const Secret &s)
{
    arg.beginStructure();
    arg << s.session << s.parameters << s.value << s.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, Secret &s)
{
    arg.beginStructure();
    arg >> s.session >> s.parameters >> s.value >> s.contentType;
    arg.endStructure();
    return arg;
}

using Attributes = QMap<QString, QString>;
using SecretMap = QMap<QDBusObjectPath, Secret>;

} // namespace
Q_DECLARE_METATYPE(Secret)
Q_DECLARE_METATYPE(SecretMap)
namespace {

Attributes ourAttributes()
{
    return {{QStringLiteral("application"), QStringLiteral("argonaut-emails")},
            {QStringLiteral("account"), g_account},
            {QStringLiteral("type"), g_kind}};
}

QDBusConnection bus()
{
    return QDBusConnection::sessionBus();
}

bool haveKeyring()
{
    if (forceFile())
        return false;
    QDBusConnectionInterface *iface = bus().interface();
    return iface && iface->isServiceRegistered(kService).value();
}

QDBusMessage call(const QString &path, const QString &iface, const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, path, iface, method);
    msg.setArguments(args);
    return bus().call(msg, QDBus::Block, 30000);
}

// Receives a prompt's Completed(dismissed, result) signal.
class PromptWaiter : public QObject
{
    Q_OBJECT

public:
    QEventLoop loop;
    bool dismissed = true;

public slots:
    void completed(bool wasDismissed, const QDBusVariant &)
    {
        dismissed = wasDismissed;
        loop.quit();
    }
};

// Runs a prompt (unlocking the keyring asks for its password) and waits.
// False if the user dismissed it.
bool runPrompt(const QDBusObjectPath &prompt)
{
    if (prompt.path().isEmpty() || prompt.path() == QLatin1String("/"))
        return true;
    PromptWaiter waiter;
    if (!bus().connect(kService, prompt.path(), QStringLiteral("org.freedesktop.Secret.Prompt"),
                       QStringLiteral("Completed"), &waiter, SLOT(completed(bool, QDBusVariant))))
        return false;
    call(prompt.path(), QStringLiteral("org.freedesktop.Secret.Prompt"), QStringLiteral("Prompt"), {QString()});
    QTimer::singleShot(120000, &waiter.loop, &QEventLoop::quit);
    waiter.loop.exec();
    return !waiter.dismissed;
}

bool openSession(QDBusObjectPath *session)
{
    const QDBusMessage reply = call(kPath, kServiceIface, QStringLiteral("OpenSession"),
                                    {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
        return false;
    *session = reply.arguments().at(1).value<QDBusObjectPath>();
    return true;
}

QList<QDBusObjectPath> paths(const QVariant &v)
{
    QList<QDBusObjectPath> out;
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg >> out;
    return out;
}

// Unlocked items carrying our attributes (unlocking them if needed).
QList<QDBusObjectPath> findItems(const Attributes &attributes = ourAttributes())
{
    const QDBusMessage reply = call(kPath, kServiceIface, QStringLiteral("SearchItems"),
                                    {QVariant::fromValue(attributes)});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
        return {};
    QList<QDBusObjectPath> unlocked = paths(reply.arguments().at(0));
    const QList<QDBusObjectPath> locked = paths(reply.arguments().at(1));
    if (!locked.isEmpty()) {
        const QDBusMessage u = call(kPath, kServiceIface, QStringLiteral("Unlock"), {QVariant::fromValue(locked)});
        if (u.type() == QDBusMessage::ReplyMessage && u.arguments().size() >= 2) {
            unlocked += paths(u.arguments().at(0));
            if (runPrompt(u.arguments().at(1).value<QDBusObjectPath>()))
                unlocked += locked;
        }
    }
    return unlocked;
}

bool keyringSave(const QString &key, QString *error);

QString readSecret(const QList<QDBusObjectPath> &items);

QString keyringLoad()
{
    return readSecret(findItems());
}

QString readSecret(const QList<QDBusObjectPath> &items)
{
    QDBusObjectPath session;
    if (items.isEmpty() || !openSession(&session))
        return {};
    const QDBusMessage reply = call(kPath, kServiceIface, QStringLiteral("GetSecrets"),
                                    {QVariant::fromValue(QList<QDBusObjectPath>{items.first()}), QVariant::fromValue(session)});
    call(session.path(), QStringLiteral("org.freedesktop.Secret.Session"), QStringLiteral("Close"), {});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    SecretMap secrets;
    reply.arguments().at(0).value<QDBusArgument>() >> secrets;
    return secrets.isEmpty() ? QString() : QString::fromUtf8(secrets.first().value);
}

bool keyringSave(const QString &key, QString *error)
{
    QDBusObjectPath session;
    if (!openSession(&session)) {
        if (error)
            *error = QObject::tr("The keyring didn't answer");
        return false;
    }
    const QDBusMessage alias = call(kPath, kServiceIface, QStringLiteral("ReadAlias"), {QStringLiteral("default")});
    QDBusObjectPath collection = alias.arguments().value(0).value<QDBusObjectPath>();
    if (collection.path().isEmpty() || collection.path() == QLatin1String("/"))
        collection = QDBusObjectPath(QStringLiteral("/org/freedesktop/secrets/aliases/default"));
    QVariantMap props;
    props.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"), QObject::tr("Argonaut Emails: %1 password (%2)").arg(g_kind.toUpper(), g_label));
    props.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"), QVariant::fromValue(ourAttributes()));
    const Secret secret{session, QByteArray(), key.toUtf8(), QStringLiteral("text/plain")};
    QDBusMessage reply = call(collection.path(), QStringLiteral("org.freedesktop.Secret.Collection"),
                              QStringLiteral("CreateItem"), {props, QVariant::fromValue(secret), true});
    if (reply.type() == QDBusMessage::ErrorMessage && reply.errorName().contains(QLatin1String("IsLocked"))) {
        // Locked collection: unlock it (the keyring asks for its password), then again.
        const QDBusMessage u = call(kPath, kServiceIface, QStringLiteral("Unlock"),
                                    {QVariant::fromValue(QList<QDBusObjectPath>{collection})});
        if (u.type() == QDBusMessage::ReplyMessage && u.arguments().size() >= 2)
            runPrompt(u.arguments().at(1).value<QDBusObjectPath>());
        reply = call(collection.path(), QStringLiteral("org.freedesktop.Secret.Collection"),
                     QStringLiteral("CreateItem"), {props, QVariant::fromValue(secret), true});
    }
    call(session.path(), QStringLiteral("org.freedesktop.Secret.Session"), QStringLiteral("Close"), {});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        if (error)
            *error = reply.errorMessage();
        return false;
    }
    if (reply.arguments().size() >= 2)
        runPrompt(reply.arguments().at(1).value<QDBusObjectPath>());
    return true;
}

bool keyringRemove()
{
    bool ok = true;
    for (const QDBusObjectPath &item : findItems()) {
        const QDBusMessage reply = call(item.path(), QStringLiteral("org.freedesktop.Secret.Item"), QStringLiteral("Delete"), {});
        ok = ok && reply.type() == QDBusMessage::ReplyMessage;
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
            runPrompt(reply.arguments().at(0).value<QDBusObjectPath>());
    }
    return ok;
}

void registerTypes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    qDBusRegisterMetaType<Secret>();
    qDBusRegisterMetaType<SecretMap>();
    qDBusRegisterMetaType<Attributes>();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
}
#endif

} // namespace

namespace Passwords {

namespace {
void select(const QString &account, const QString &kind, const QString &label = QString())
{
    g_account = account;
    g_kind = kind;
    g_label = label;
}
} // namespace

QString load(const QString &account, const QString &kind, Where *where)
{
    select(account, kind);
#if defined(MAIL_DBUS)
    registerTypes();
    if (haveKeyring()) {
        const QString key = keyringLoad();
        if (where)
            *where = key.isEmpty() ? Where::None : Where::Keyring;
        if (!key.isEmpty())
            return key;
    }
#endif
    const QString key = readFile();
    if (where)
        *where = key.isEmpty() ? Where::None : Where::File;
    return key;
}

bool save(const QString &account, const QString &kind, const QString &password, const QString &label, Where *where, QString *error)
{
    select(account, kind, label);
#if defined(MAIL_DBUS)
    registerTypes();
    if (haveKeyring()) {
        keyringRemove(); // one item for each account and kind
        if (keyringSave(password, error)) {
            QFile::remove(keyFile()); // never in two places
            if (where)
                *where = Where::Keyring;
            return true;
        }
        return false; // don't fall back silently to a file when the keyring is there
    }
#endif
    if (!writeFile(password, error))
        return false;
    if (where)
        *where = Where::File;
    return true;
}

bool remove(const QString &account, const QString &kind)
{
    select(account, kind);
    bool ok = true;
#if defined(MAIL_DBUS)
    registerTypes();
    if (haveKeyring())
        ok = keyringRemove();
#endif
    if (QFile::exists(keyFile()) && !QFile::remove(keyFile()))
        ok = false;
    return ok;
}

QString describe(Where where)
{
    switch (where) {
    case Where::Keyring:
        return QObject::tr("your keyring");
    case Where::File:
        return QObject::tr("a private file (no keyring found)");
    case Where::None:
        break;
    }
    return {};
}

} // namespace Passwords

#if defined(MAIL_DBUS)
#include "Passwords.moc"
#endif
