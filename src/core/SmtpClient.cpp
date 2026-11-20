#include "SmtpClient.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QSslSocket>

namespace {

bool isThisComputer(const QString &host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

// A recipient or sender as SMTP takes it: no line breaks, no angle brackets.
bool isSafeAddress(const QString &address)
{
    if (address.isEmpty() || address.size() > 320)
        return false;
    for (const QChar c : address) {
        if (c.unicode() < 0x21 || c == QLatin1Char('<') || c == QLatin1Char('>'))
            return false;
    }
    return address.contains(QLatin1Char('@'));
}

} // namespace

QByteArray dotStuffed(const QByteArray &message)
{
    QByteArray text = message;
    text.replace("\r\n", "\n");
    text.replace('\r', "\n");
    QByteArray out;
    out.reserve(text.size() + text.size() / 40);
    for (const QByteArray &line : text.split('\n')) {
        if (line.startsWith('.'))
            out.append('.');
        out.append(line).append("\r\n");
    }
    if (text.endsWith('\n'))
        out.chop(2); // split() made one empty line too many
    return out;
}

SmtpClient::SmtpClient(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { fail(tr("The mail server stopped answering")); });
}

SmtpClient::~SmtpClient()
{
    m_step = Step::Done;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void SmtpClient::send(const Settings &settings, const QString &from, const QStringList &recipients, const QByteArray &message)
{
    m_step = Step::Greeting; // from here on a failure is said
    m_settings = settings;
    m_from = from;
    m_recipients = recipients;
    m_nextRecipient = 0;
    m_message = message;
    m_buffer.clear();
    m_lines.clear();
    m_extensions.clear();
    if (settings.security == Security::None && !isThisComputer(settings.host)) {
        QTimer::singleShot(0, this, [this] { fail(tr("A connection without encryption is only allowed to this computer")); });
        return;
    }
    if (!isSafeAddress(from)) {
        QTimer::singleShot(0, this, [this] { fail(tr("The sender's address isn't valid")); });
        return;
    }
    if (recipients.isEmpty()) {
        QTimer::singleShot(0, this, [this] { fail(tr("The message has no recipients")); });
        return;
    }
    for (const QString &r : recipients) {
        if (!isSafeAddress(r)) {
            QTimer::singleShot(0, this, [this, r] { fail(tr("“%1” isn't a valid address").arg(r)); });
            return;
        }
    }
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    m_socket = new QSslSocket(this);
    connect(m_socket, &QSslSocket::readyRead, this, &SmtpClient::onReadyRead);
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_step != Step::Done)
            fail(m_socket->errorString());
    });
    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        fail(tr("The mail server's certificate can't be trusted: %1").arg(errors.value(0).errorString()));
    });
    m_step = Step::Greeting;
    m_timer.start(m_timeoutMs);
    if (settings.security == Security::Tls)
        m_socket->connectToHostEncrypted(settings.host, settings.port);
    else
        m_socket->connectToHost(settings.host, settings.port);
}

void SmtpClient::write(const QByteArray &line)
{
    m_timer.start(m_timeoutMs);
    m_socket->write(line + "\r\n");
}

void SmtpClient::onReadyRead()
{
    m_buffer += m_socket->readAll();
    if (m_timer.isActive())
        m_timer.start(m_timeoutMs);
    if (m_buffer.size() > 1024 * 1024) {
        fail(tr("The mail server sent too much"));
        return;
    }
    while (m_socket) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0)
            return;
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.size() < 3)
            continue;
        const int code = line.left(3).toInt();
        m_lines.append(QString::fromUtf8(line.mid(4)));
        if (line.size() > 3 && line.at(3) == '-')
            continue; // more lines of this reply
        const QStringList lines = m_lines;
        m_lines.clear();
        reply(code, lines);
    }
}

void SmtpClient::ehlo()
{
    QString name = QHostInfo::localHostName();
    if (name.isEmpty() || name.contains(QLatin1Char(' ')))
        name = QStringLiteral("localhost");
    write("EHLO " + name.toLatin1());
}

void SmtpClient::afterEhlo(const QStringList &lines)
{
    m_extensions.clear();
    for (const QString &l : lines)
        m_extensions.append(l.toUpper());
    if (m_step == Step::Ehlo && m_settings.security == Security::StartTls) {
        if (!m_extensions.contains(QStringLiteral("STARTTLS"))) {
            fail(tr("The mail server doesn't offer encryption (STARTTLS)"));
            return;
        }
        m_step = Step::StartTls;
        write("STARTTLS");
        return;
    }
    if (m_settings.user.isEmpty()) {
        mailFrom();
        return;
    }
    QString auth;
    for (const QString &e : std::as_const(m_extensions)) {
        if (e.startsWith(QLatin1String("AUTH ")) || e.startsWith(QLatin1String("AUTH=")))
            auth += QLatin1Char(' ') + e.mid(5) + QLatin1Char(' ');
    }
    const QByteArray user = m_settings.user.toUtf8();
    const QByteArray password = m_settings.password.toUtf8();
    if (auth.contains(QLatin1String(" PLAIN ")) || auth.isEmpty()) {
        m_step = Step::Auth;
        write("AUTH PLAIN " + QByteArray().append('\0').append(user).append('\0').append(password).toBase64());
    } else if (auth.contains(QLatin1String(" LOGIN "))) {
        m_step = Step::AuthUser;
        write("AUTH LOGIN");
    } else {
        fail(tr("The mail server offers no way of logging in that Argonaut Emails knows"));
    }
}

void SmtpClient::mailFrom()
{
    m_step = Step::MailFrom;
    QByteArray line = "MAIL FROM:<" + m_from.toUtf8() + '>';
    if (m_extensions.contains(QStringLiteral("SMTPUTF8")) && m_from.toUtf8() != m_from.toLatin1())
        line += " SMTPUTF8";
    write(line);
}

void SmtpClient::reply(int code, const QStringList &lines)
{
    const QString text = lines.join(QLatin1Char(' '));
    switch (m_step) {
    case Step::Greeting:
        if (code != 220)
            return fail(tr("The mail server refused the connection: %1").arg(text));
        m_step = Step::Ehlo;
        ehlo();
        return;
    case Step::Ehlo:
    case Step::EhloAgain:
        if (code != 250)
            return fail(tr("The mail server didn't accept the greeting: %1").arg(text));
        afterEhlo(lines.mid(1));
        return;
    case Step::StartTls:
        if (code != 220)
            return fail(tr("The mail server refused encryption: %1").arg(text));
        m_buffer.clear();
        connect(m_socket, &QSslSocket::encrypted, this, [this] {
            m_step = Step::EhloAgain;
            ehlo();
        });
        m_socket->startClientEncryption();
        return;
    case Step::Auth:
        if (code != 235)
            return fail(tr("The mail server didn't accept the user name or password: %1").arg(text));
        mailFrom();
        return;
    case Step::AuthUser:
        if (code != 334)
            return fail(tr("The mail server refused logging in: %1").arg(text));
        m_step = Step::AuthPassword;
        write(m_settings.user.toUtf8().toBase64());
        return;
    case Step::AuthPassword:
        if (code == 334) {
            write(m_settings.password.toUtf8().toBase64());
            m_step = Step::Auth;
            return;
        }
        return fail(tr("The mail server refused logging in: %1").arg(text));
    case Step::MailFrom:
        if (code != 250)
            return fail(tr("The mail server refused the sender: %1").arg(text));
        m_step = Step::RcptTo;
        write("RCPT TO:<" + m_recipients.at(m_nextRecipient++).toUtf8() + '>');
        return;
    case Step::RcptTo:
        if (code != 250 && code != 251)
            return fail(tr("The mail server refused “%1”: %2").arg(m_recipients.at(m_nextRecipient - 1), text));
        if (m_nextRecipient < m_recipients.size()) {
            write("RCPT TO:<" + m_recipients.at(m_nextRecipient++).toUtf8() + '>');
            return;
        }
        m_step = Step::Data;
        write("DATA");
        return;
    case Step::Data:
        if (code != 354)
            return fail(tr("The mail server refused the message: %1").arg(text));
        m_step = Step::Body;
        m_timer.start(m_timeoutMs);
        m_socket->write(dotStuffed(m_message) + ".\r\n");
        return;
    case Step::Body:
        if (code != 250)
            return fail(tr("The mail server refused the message: %1").arg(text));
        m_step = Step::Quit;
        write("QUIT");
        finish();
        return;
    case Step::Quit:
    case Step::Done:
        return;
    }
}

void SmtpClient::finish()
{
    m_step = Step::Done;
    m_timer.stop();
    if (m_socket)
        m_socket->disconnectFromHost();
    emit sent();
}

void SmtpClient::fail(const QString &message)
{
    if (m_step == Step::Done)
        return;
    m_step = Step::Done;
    m_timer.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    emit failed(message);
}
