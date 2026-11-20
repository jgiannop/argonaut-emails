#include "Account.h"

#include "Passwords.h"

#include <QSettings>
#include <QUuid>

namespace {

QString securityKey(ImapClient::Security s)
{
    switch (s) {
    case ImapClient::Security::Tls:
        return QStringLiteral("tls");
    case ImapClient::Security::StartTls:
        return QStringLiteral("starttls");
    case ImapClient::Security::None:
        break;
    }
    return QStringLiteral("none");
}

ImapClient::Security securityOf(const QString &key)
{
    if (key == QLatin1String("starttls"))
        return ImapClient::Security::StartTls;
    if (key == QLatin1String("none"))
        return ImapClient::Security::None;
    return ImapClient::Security::Tls;
}

bool isId(const QString &id)
{
    return !QUuid::fromString(QLatin1Char('{') + id + QLatin1Char('}')).isNull();
}

Account read(QSettings &s, const QString &id)
{
    Account a;
    s.beginGroup(QStringLiteral("accounts/") + id);
    a.id = id;
    a.name = s.value(QStringLiteral("name")).toString();
    a.email = s.value(QStringLiteral("email")).toString();
    a.imapHost = s.value(QStringLiteral("imap/host")).toString();
    a.imapPort = quint16(s.value(QStringLiteral("imap/port"), 993).toUInt());
    a.imapSecurity = securityOf(s.value(QStringLiteral("imap/security")).toString());
    a.imapUser = s.value(QStringLiteral("imap/user")).toString();
    a.smtpHost = s.value(QStringLiteral("smtp/host")).toString();
    a.smtpPort = quint16(s.value(QStringLiteral("smtp/port"), 465).toUInt());
    a.smtpSecurity = securityOf(s.value(QStringLiteral("smtp/security")).toString());
    a.smtpUser = s.value(QStringLiteral("smtp/user")).toString();
    s.endGroup();
    return a;
}

} // namespace

QString securityName(ImapClient::Security security)
{
    switch (security) {
    case ImapClient::Security::Tls:
        return QObject::tr("SSL/TLS");
    case ImapClient::Security::StartTls:
        return QObject::tr("STARTTLS");
    case ImapClient::Security::None:
        break;
    }
    return QObject::tr("None (this computer only)");
}

ImapClient::Settings Account::imapSettings() const
{
    ImapClient::Settings s;
    s.host = imapHost;
    s.port = imapPort;
    s.security = imapSecurity;
    s.user = imapUser.isEmpty() ? email : imapUser;
    s.password = Passwords::load(id, QStringLiteral("imap"));
    return s;
}

SmtpClient::Settings Account::smtpSettings() const
{
    SmtpClient::Settings s;
    s.host = smtpHost;
    s.port = smtpPort;
    s.security = smtpSecurity;
    s.user = smtpUser.isEmpty() ? email : smtpUser;
    s.password = Passwords::load(id, QStringLiteral("smtp"));
    if (s.password.isEmpty()) // most providers: the same password
        s.password = Passwords::load(id, QStringLiteral("imap"));
    return s;
}

QList<Account> Account::all()
{
    QSettings s;
    const QStringList ids = s.value(QStringLiteral("accounts/order")).toStringList();
    QList<Account> out;
    for (const QString &id : ids) {
        if (!isId(id))
            continue;
        const Account a = read(s, id);
        if (a.isValid())
            out.append(a);
    }
    return out;
}

Account Account::byId(const QString &id)
{
    if (!isId(id))
        return {};
    QSettings s;
    return read(s, id);
}

void Account::save(Account &a)
{
    if (a.id.isEmpty())
        a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSettings s;
    s.beginGroup(QStringLiteral("accounts/") + a.id);
    s.setValue(QStringLiteral("name"), a.name);
    s.setValue(QStringLiteral("email"), a.email);
    s.setValue(QStringLiteral("imap/host"), a.imapHost);
    s.setValue(QStringLiteral("imap/port"), a.imapPort);
    s.setValue(QStringLiteral("imap/security"), securityKey(a.imapSecurity));
    s.setValue(QStringLiteral("imap/user"), a.imapUser);
    s.setValue(QStringLiteral("smtp/host"), a.smtpHost);
    s.setValue(QStringLiteral("smtp/port"), a.smtpPort);
    s.setValue(QStringLiteral("smtp/security"), securityKey(a.smtpSecurity));
    s.setValue(QStringLiteral("smtp/user"), a.smtpUser);
    s.endGroup();
    QStringList ids = s.value(QStringLiteral("accounts/order")).toStringList();
    if (!ids.contains(a.id))
        ids.append(a.id);
    s.setValue(QStringLiteral("accounts/order"), ids);
}

void Account::remove(const QString &id)
{
    if (!isId(id))
        return;
    QSettings s;
    s.remove(QStringLiteral("accounts/") + id);
    QStringList ids = s.value(QStringLiteral("accounts/order")).toStringList();
    ids.removeAll(id);
    s.setValue(QStringLiteral("accounts/order"), ids);
    Passwords::remove(id, QStringLiteral("imap"));
    Passwords::remove(id, QStringLiteral("smtp"));
}

Account Account::guessFor(const QString &email)
{
    Account a;
    a.email = email.trimmed();
    const QString domain = a.email.section(QLatin1Char('@'), 1).toLower();
    struct Known
    {
        const char *domain;
        const char *imap;
        const char *smtp;
        quint16 smtpPort;
        ImapClient::Security smtpSecurity;
    };
    static const Known known[] = {
        {"gmail.com", "imap.gmail.com", "smtp.gmail.com", 465, ImapClient::Security::Tls},
        {"googlemail.com", "imap.gmail.com", "smtp.gmail.com", 465, ImapClient::Security::Tls},
        {"outlook.com", "outlook.office365.com", "smtp-mail.outlook.com", 587, ImapClient::Security::StartTls},
        {"hotmail.com", "outlook.office365.com", "smtp-mail.outlook.com", 587, ImapClient::Security::StartTls},
        {"live.com", "outlook.office365.com", "smtp-mail.outlook.com", 587, ImapClient::Security::StartTls},
        {"yahoo.com", "imap.mail.yahoo.com", "smtp.mail.yahoo.com", 465, ImapClient::Security::Tls},
        {"icloud.com", "imap.mail.me.com", "smtp.mail.me.com", 587, ImapClient::Security::StartTls},
        {"me.com", "imap.mail.me.com", "smtp.mail.me.com", 587, ImapClient::Security::StartTls},
        {"gmx.com", "imap.gmx.com", "mail.gmx.com", 465, ImapClient::Security::Tls},
    };
    for (const Known &k : known) {
        if (domain == QLatin1String(k.domain)) {
            a.imapHost = QString::fromLatin1(k.imap);
            a.smtpHost = QString::fromLatin1(k.smtp);
            a.smtpPort = k.smtpPort;
            a.smtpSecurity = k.smtpSecurity;
            return a;
        }
    }
    if (!domain.isEmpty()) {
        a.imapHost = QStringLiteral("imap.") + domain;
        a.smtpHost = QStringLiteral("smtp.") + domain;
    }
    return a;
}
