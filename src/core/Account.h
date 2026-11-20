#pragma once

#include "ImapClient.h"
#include "SmtpClient.h"

#include <QList>
#include <QString>

// A mail account: who you are, where mail is read (IMAP) and where it is
// sent from (SMTP). Kept in the settings ("accounts/<id>/…"); the passwords
// are not: they are in Passwords (the keyring).
struct Account
{
    QString id;   // made here, a UUID without braces
    QString name; // "Maria Papadopoulou": what others see as the sender
    QString email;

    QString imapHost;
    quint16 imapPort = 993;
    ImapClient::Security imapSecurity = ImapClient::Security::Tls;
    QString imapUser;

    QString smtpHost;
    quint16 smtpPort = 465;
    ImapClient::Security smtpSecurity = ImapClient::Security::Tls;
    QString smtpUser;

    bool isValid() const { return !id.isEmpty() && !email.isEmpty() && !imapHost.isEmpty(); }
    QString title() const { return email; }

    ImapClient::Settings imapSettings() const;  // with the password from the keyring
    SmtpClient::Settings smtpSettings() const;

    static QList<Account> all();
    static Account byId(const QString &id);
    // Saves it (a new one gets an id first).
    static void save(Account &account);
    // Its settings and passwords; the mail it had is the store's to forget.
    static void remove(const QString &id);

    // Servers of the big providers, from the address: a start the user can change.
    static Account guessFor(const QString &email);
};

QString securityName(ImapClient::Security security);
