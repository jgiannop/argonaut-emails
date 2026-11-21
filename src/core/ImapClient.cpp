#include "ImapClient.h"

#include <QDateTime>
#include <QHostAddress>
#include <QSslSocket>

namespace {

bool isThisComputer(const QString &host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

} // namespace

ImapClient::ImapClient(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { fail(tr("The server stopped answering")); });
}

ImapClient::~ImapClient()
{
    // Nobody hears these any more.
    m_queue.clear();
    m_waiting.clear();
    m_state = State::Closed; // a socket that closes now says nothing
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void ImapClient::open(const Settings &settings)
{
    close();
    m_settings = settings;
    if (settings.security == Security::None && !isThisComputer(settings.host)) {
        // Never a password in the clear over a network.
        QTimer::singleShot(0, this, [this] { emit failed(tr("A connection without encryption is only allowed to this computer")); });
        return;
    }
    m_socket = new QSslSocket(this);
    connect(m_socket, &QSslSocket::readyRead, this, &ImapClient::onReadyRead);
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_state != State::Closed)
            fail(m_socket->errorString());
    });
    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        // Not ignored: a certificate that can't be checked ends the connection.
        fail(tr("The server's certificate can't be trusted: %1").arg(errors.value(0).errorString()));
    });
    connect(m_socket, &QSslSocket::disconnected, this, [this] {
        if (m_state != State::Closed)
            fail(tr("The server closed the connection"));
    });
    m_state = State::Connecting;
    m_timer.start(m_timeoutMs);
    if (settings.security == Security::Tls) {
        connect(m_socket, &QSslSocket::encrypted, this, &ImapClient::onConnected);
        m_socket->connectToHostEncrypted(settings.host, settings.port);
    } else {
        connect(m_socket, &QSslSocket::connected, this, &ImapClient::onConnected);
        m_socket->connectToHost(settings.host, settings.port);
    }
}

void ImapClient::close()
{
    const QList<Pending> dropped = shutDown();
    for (const Pending &p : dropped) {
        if (p.done)
            p.done(Result{});
    }
}

QList<ImapClient::Pending> ImapClient::shutDown()
{
    m_state = State::Closed;
    m_timer.stop();
    m_buffer.clear();
    m_capabilities.clear();
    m_busy = false;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    QList<Pending> dropped = m_queue + m_waiting;
    m_queue.clear();
    m_waiting.clear();
    return dropped;
}

void ImapClient::probe(int timeoutMs, std::function<void(bool alive)> done)
{
    const int normal = m_timeoutMs;
    m_timeoutMs = timeoutMs;
    command("NOOP", [this, normal, done](const Result &r) {
        m_timeoutMs = normal;
        done(!r.status.isEmpty()); // any answer, even BAD: someone is there
    });
}

void ImapClient::onConnected()
{
    if (m_state != State::Connecting)
        return;
    m_state = State::Greeting;
    m_timer.start(m_timeoutMs);
}

void ImapClient::command(const QByteArray &line, Done done)
{
    Pending p;
    p.line = line;
    p.done = std::move(done);
    if (m_state != State::Ready) {
        if (m_state == State::Closed) {
            QTimer::singleShot(0, this, [d = p.done] {
                if (d)
                    d(Result{});
            });
            return;
        }
        m_waiting.append(p);
        return;
    }
    m_queue.append(p);
    sendNext();
}

void ImapClient::commandWithLiteral(const QByteArray &line, const QByteArray &data, Done done)
{
    Pending p;
    p.line = line + " {" + QByteArray::number(data.size()) + '}';
    p.continuation = data;
    p.done = std::move(done);
    if (m_state == State::Closed) {
        QTimer::singleShot(0, this, [d = p.done] {
            if (d)
                d(Result{});
        });
        return;
    }
    if (m_state != State::Ready) {
        m_waiting.append(p);
        return;
    }
    m_queue.append(p);
    sendNext();
}

void ImapClient::send(const QByteArray &line, const QByteArray &continuation, Done done, bool front)
{
    Pending p;
    p.line = line;
    p.continuation = continuation;
    p.done = std::move(done);
    if (front)
        m_queue.prepend(p);
    else
        m_queue.append(p);
    sendNext();
}

void ImapClient::sendNext()
{
    if (m_busy || m_queue.isEmpty() || !m_socket)
        return;
    Pending &p = m_queue.first();
    p.tag = "A" + QByteArray::number(m_nextTag++);
    m_busy = true;
    m_socket->write(p.tag + ' ' + p.line + "\r\n");
    m_timer.start(m_timeoutMs);
}

void ImapClient::onReadyRead()
{
    m_buffer += m_socket->readAll();
    m_lastHeard = QDateTime::currentMSecsSinceEpoch();
    if (m_timer.isActive())
        m_timer.start(m_timeoutMs); // data is coming: a big message on a slow line isn't a dead server
    QByteArray one;
    bool tooBig = false;
    while (m_socket && Imap::takeResponse(m_buffer, &one, &tooBig))
        handle(Imap::parseResponse(one));
    if (tooBig)
        fail(tr("The server sent more than can be read at once"));
}

void ImapClient::readCapabilities(const Imap::Response &response)
{
    QList<QByteArray> words;
    if (response.code.toUpper().startsWith("CAPABILITY "))
        words = response.code.mid(11).split(' ');
    else if (!response.values.isEmpty() && response.values.first().is("CAPABILITY")) {
        for (qsizetype i = 1; i < response.values.size(); ++i)
            words.append(response.values.at(i).bytes());
    }
    if (words.isEmpty())
        return;
    m_capabilities.clear();
    for (const QByteArray &w : std::as_const(words)) {
        if (!w.isEmpty())
            m_capabilities.insert(w.toUpper());
    }
}

void ImapClient::handle(const Imap::Response &response)
{
    m_timer.start(m_timeoutMs); // it is talking
    readCapabilities(response);
    if (m_state == State::Greeting) {
        if (response.isUntagged() && response.status == "BYE") {
            fail(response.text.isEmpty() ? tr("The server refused the connection") : response.text);
            return;
        }
        if (response.isUntagged() && (response.status == "OK" || response.status == "PREAUTH")) {
            if (response.status == "PREAUTH" && m_settings.security != Security::StartTls) {
                m_state = State::Ready;
                m_queue.append(m_waiting);
                m_waiting.clear();
                m_timer.stop();
                emit ready();
                sendNext();
                return;
            }
            afterGreeting();
        }
        return;
    }
    if (response.isContinuation()) {
        if (!m_queue.isEmpty() && m_busy && !m_queue.first().continuation.isEmpty()) {
            m_socket->write(m_queue.first().continuation + "\r\n");
            m_queue.first().continuation.clear();
        } else if (m_socket) {
            m_socket->write("*\r\n"); // nothing to give: cancel
        }
        return;
    }
    if (response.isUntagged()) {
        if (response.status == "BYE" && m_state == State::Ready && (m_queue.isEmpty() || !m_queue.first().line.startsWith("LOGOUT"))) {
            fail(response.text.isEmpty() ? tr("The server ended the connection") : response.text);
            return;
        }
        if (m_busy && !m_queue.isEmpty())
            m_queue.first().untagged.append(response);
        else
            emit unsolicited(response);
        return;
    }
    // Tagged: the end of the command that runs.
    if (!m_busy || m_queue.isEmpty() || m_queue.first().tag != response.tag)
        return; // not ours
    Pending p = m_queue.takeFirst();
    m_busy = false;
    m_timer.stop();
    Result result;
    result.status = response.status;
    result.ok = response.status == "OK";
    result.code = response.code;
    result.text = response.text;
    result.untagged = p.untagged;
    if (p.done)
        p.done(result);
    sendNext();
}

void ImapClient::afterGreeting()
{
    m_state = State::Authenticating; // the answers that follow are to our commands
    auto next = [this] {
        if (m_settings.security == Security::StartTls) {
            if (!hasCapability("STARTTLS")) {
                fail(tr("The server doesn't offer encryption (STARTTLS)"));
                return;
            }
            m_state = State::StartTls;
            send("STARTTLS", {}, [this](const Result &r) {
                if (!r.ok) {
                    fail(tr("The server refused encryption: %1").arg(r.text));
                    return;
                }
                m_buffer.clear(); // nothing said before encryption counts
                m_capabilities.clear();
                connect(m_socket, &QSslSocket::encrypted, this, [this] {
                    send("CAPABILITY", {}, [this](const Result &c) {
                        if (c.ok)
                            authenticate();
                        else
                            fail(c.text);
                    });
                });
                m_socket->startClientEncryption();
            });
            return;
        }
        authenticate();
    };
    if (m_capabilities.isEmpty()) {
        send("CAPABILITY", {}, [this, next](const Result &r) {
            if (!r.ok) {
                fail(r.text);
                return;
            }
            next();
        });
    } else {
        next();
    }
}

void ImapClient::authenticate()
{
    m_state = State::Authenticating;
    if (hasCapability("LOGINDISABLED") && !hasCapability("AUTH=PLAIN")) {
        fail(tr("The server doesn't allow logging in here"));
        return;
    }
    auto loggedIn = [this](const Result &r) {
        if (!r.ok) {
            fail(r.text.isEmpty() ? tr("The user name or the password is wrong") : r.text);
            return;
        }
        auto nowReady = [this] {
            m_state = State::Ready;
            m_queue.append(m_waiting);
            m_waiting.clear();
            emit ready();
            sendNext();
        };
        if (r.code.toUpper().startsWith("CAPABILITY ")) {
            readCapabilities(Imap::parseResponse("* OK [" + r.code + "] x"));
            nowReady();
            return;
        }
        // What a server can do may change once logged in (Gmail adds MOVE
        // and UIDPLUS): asked again (RFC 3501 6.2).
        send("CAPABILITY", {}, [nowReady](const Result &) { nowReady(); });
    };
    const QByteArray plain = QByteArray().append('\0').append(m_settings.user.toUtf8()).append('\0').append(m_settings.password.toUtf8()).toBase64();
    if (hasCapability("AUTH=PLAIN")) {
        if (hasCapability("SASL-IR"))
            send("AUTHENTICATE PLAIN " + plain, {}, loggedIn);
        else
            send("AUTHENTICATE PLAIN", plain, loggedIn);
        return;
    }
    bool okUser = false;
    bool okPassword = false;
    const QByteArray user = Imap::quoted(m_settings.user.toUtf8(), &okUser);
    const QByteArray password = Imap::quoted(m_settings.password.toUtf8(), &okPassword);
    if (!okUser || !okPassword) {
        fail(tr("The user name or password has a character that can't be sent"));
        return;
    }
    send("LOGIN " + user + ' ' + password, {}, loggedIn);
}

void ImapClient::fail(const QString &message)
{
    if (m_state == State::Closed)
        return;
    // The owner hears of it before the commands that were waiting do: it
    // decides first whether to connect again and go on.
    const QList<Pending> dropped = shutDown();
    emit failed(message);
    for (const Pending &p : dropped) {
        if (p.done)
            p.done(Result{});
    }
}
