#pragma once

#include "ImapParser.h"

#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>

class QSslSocket;

// One connection to an IMAP server (RFC 3501), on Qt's QSslSocket.
//
// Commands go one at a time, in order (no pipelining: simpler, and fast
// enough for a mail client that fetches in batches). Each gets the
// untagged responses that came while it ran and the tagged answer.
//
// Security: a password is only ever sent over TLS (implicit, port 993, or
// STARTTLS); certificates are checked by Qt and an error ends the
// connection. A connection without TLS is allowed only to this computer
// (127.0.0.1, ::1, localhost), which is what the tests use.
class ImapClient : public QObject
{
    Q_OBJECT

public:
    enum class Security { Tls, StartTls, None };

    struct Settings
    {
        QString host;
        quint16 port = 993;
        Security security = Security::Tls;
        QString user;
        QString password;
    };

    struct Result
    {
        bool ok = false;
        QByteArray status; // OK, NO, BAD; empty when the connection failed
        QByteArray code;   // the bracketed response code
        QString text;
        QList<Imap::Response> untagged;
    };
    using Done = std::function<void(const Result &result)>;

    explicit ImapClient(QObject *parent = nullptr);
    ~ImapClient() override;

    // Connects and logs in; `ready` or `failed` follows.
    void open(const Settings &settings);
    void close();
    bool isReady() const { return m_state == State::Ready; }
    bool hasCapability(const QByteArray &name) const { return m_capabilities.contains(name.toUpper()); }

    // Sends "<tag> <line>" when the commands before it are done.
    void command(const QByteArray &line, Done done);
    // A command that ends with a literal ("APPEND Sent (\\Seen) {n}"):
    // `data` is sent when the server says go on.
    void commandWithLiteral(const QByteArray &line, const QByteArray &data, Done done);

    // How long a command may take before the connection is given up.
    void setTimeout(int ms) { m_timeoutMs = ms; }
    // When the server last said something (ms since the epoch, by the wall
    // clock: a computer that slept counts as time gone by).
    qint64 lastHeard() const { return m_lastHeard; }
    // A NOOP that must be answered within `timeoutMs`: is the line still there?
    // A dead one fails (`failed` first), then `done(false)`.
    void probe(int timeoutMs, std::function<void(bool alive)> done);

    // IDLE (RFC 2177), on a selected folder: the server tells at once what
    // changes there. Each untagged response that comes meanwhile goes to
    // `heard` ("* 6 EXISTS"); no timeout runs while idling (the owner
    // renews it). `done` when it ends: after stopIdle(), the server's OK; on
    // a lost line an empty status. Only when the server has IDLE.
    void idle(std::function<void(const Imap::Response &response)> heard, Done done);
    // Ends the IDLE ("DONE"); its OK must come within the timeout, so this
    // is also how a quiet line is found dead.
    void stopIdle();
    bool isIdling() const { return m_idling || m_idleWaiting; }

signals:
    void ready();
    void failed(const QString &message);
    // A response that came between commands (a new message: "* 5 EXISTS").
    void unsolicited(const Imap::Response &response);

private:
    enum class State { Closed, Connecting, Greeting, StartTls, Authenticating, Ready };

    struct Pending
    {
        QByteArray tag;
        QByteArray line;
        QByteArray continuation; // sent on "+" (AUTHENTICATE without SASL-IR)
        bool idle = false;       // IDLE: "+" means it is idling, nothing is sent back
        Done done;
        QList<Imap::Response> untagged;
    };

    void onConnected();
    void onReadyRead();
    void handle(const Imap::Response &response);
    void sendNext();
    void send(const QByteArray &line, const QByteArray &continuation, Done done, bool front = false);
    void readCapabilities(const Imap::Response &response);
    void afterGreeting();
    void authenticate();
    void fail(const QString &message);
    QList<Pending> shutDown(); // closes; what was waiting, for its callbacks

    QSslSocket *m_socket = nullptr;
    Settings m_settings;
    State m_state = State::Closed;
    QByteArray m_buffer;
    QList<Pending> m_queue;   // being sent, in order
    QList<Pending> m_waiting; // the user's, until logged in
    bool m_busy = false;
    int m_nextTag = 1;
    QSet<QByteArray> m_capabilities;
    QTimer m_timer;
    int m_timeoutMs = 60000;
    qint64 m_lastHeard = 0;
    std::function<void(const Imap::Response &)> m_heard; // while idling
    bool m_idleWaiting = false; // IDLE sent, "+" not come yet
    bool m_idling = false;      // "+" came: DONE ends it
    bool m_stopWhenIdling = false; // stopIdle() before the "+"
};
