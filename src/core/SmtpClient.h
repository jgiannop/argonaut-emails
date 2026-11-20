#pragma once

#include "ImapClient.h"

#include <QObject>
#include <QStringList>
#include <QTimer>

class QSslSocket;

// Sends one message (RFC 5321) and disconnects: EHLO, STARTTLS when asked,
// AUTH PLAIN or LOGIN, MAIL FROM, RCPT TO for each recipient, DATA.
// The same rule as for IMAP: a password only over TLS, plain connections
// only to this computer (the tests).
class SmtpClient : public QObject
{
    Q_OBJECT

public:
    using Security = ImapClient::Security;

    struct Settings
    {
        QString host;
        quint16 port = 465;
        Security security = Security::Tls;
        QString user;     // empty: no login (a server on this computer)
        QString password;
    };

    explicit SmtpClient(QObject *parent = nullptr);
    ~SmtpClient() override;

    // `message` is the whole RFC 5322 message, CRLF line ends.
    void send(const Settings &settings, const QString &from, const QStringList &recipients, const QByteArray &message);
    void setTimeout(int ms) { m_timeoutMs = ms; }

signals:
    void sent();
    void failed(const QString &message);

private:
    enum class Step { Greeting, Ehlo, StartTls, EhloAgain, Auth, AuthUser, AuthPassword, MailFrom, RcptTo, Data, Body, Quit, Done };

    void onReadyRead();
    void reply(int code, const QStringList &lines);
    void write(const QByteArray &line);
    void ehlo();
    void afterEhlo(const QStringList &lines);
    void mailFrom();
    void fail(const QString &message);
    void finish();

    QSslSocket *m_socket = nullptr;
    Settings m_settings;
    QString m_from;
    QStringList m_recipients;
    int m_nextRecipient = 0;
    QByteArray m_message;
    QByteArray m_buffer;
    QStringList m_lines; // of a reply that goes on ("250-…")
    Step m_step = Step::Done;
    QStringList m_extensions;
    QTimer m_timer;
    int m_timeoutMs = 60000;
};

// The message with "." doubled at the start of lines and CRLF line ends.
QByteArray dotStuffed(const QByteArray &message);
