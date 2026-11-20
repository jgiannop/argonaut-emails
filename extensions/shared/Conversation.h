#pragma once

// The conversation with the program, for the extensions that come with it (Argonaut's, as it is there): a
// JSON object a line, read from standard input and written to standard
// output (docs/extensions.md). Every extension had its own copy of this
// loop; it is here once.

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>

#include <cerrno>
#include <cstdio>
#include <functional>
#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#include <thread>
#else
#include <unistd.h>
#endif

// Says something to the program.
inline void send(const QJsonObject &message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
}

// Hears what the program says: `handle` gets each message as it comes, and the
// application quits when the program closes the pipe (or is gone). Make one in
// main(), after the application object, and keep it while the program runs.
//
// Several messages can arrive in one go (arrow keys held down in the file
// list ask for one preview after another): `afterAll`, if given, is called
// once after the last of them, for an extension that only answers the
// newest.
class Conversation
{
public:
#if defined(Q_OS_WIN)
    // Windows can't be asked to say when a pipe has something to read: a
    // thread waits in a read and hands what came to the program's own thread.
    // Lines go as they are, without Windows' own line endings.
    explicit Conversation(std::function<void(const QJsonObject &)> handle, std::function<void()> afterAll = {})
        : m_handle(std::move(handle))
        , m_afterAll(std::move(afterAll))
    {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        std::thread([this] {
            char chunk[65536];
            for (;;) {
                const int got = _read(_fileno(stdin), chunk, sizeof chunk);
                const QByteArray data = got > 0 ? QByteArray(chunk, got) : QByteArray();
                QMetaObject::invokeMethod(&m_context, [this, data] { take(data); }, Qt::QueuedConnection);
                if (got <= 0)
                    return;
            }
        }).detach();
    }

private:
    QObject m_context;
#else
    explicit Conversation(std::function<void(const QJsonObject &)> handle, std::function<void()> afterAll = {})
        : m_input(STDIN_FILENO, QSocketNotifier::Read)
        , m_handle(std::move(handle))
        , m_afterAll(std::move(afterAll))
    {
        QObject::connect(&m_input, &QSocketNotifier::activated, &m_input, [this] { read(); });
    }

private:
    void read()
    {
        char chunk[65536];
        const ssize_t got = ::read(STDIN_FILENO, chunk, sizeof chunk);
        // A signal came in between (a helper program of ours ended): that is
        // not the pipe closing. The notifier calls again when there is more.
        if (got < 0 && (errno == EINTR || errno == EAGAIN))
            return;
        if (got <= 0)
            m_input.setEnabled(false);
        take(got > 0 ? QByteArray(chunk, qsizetype(got)) : QByteArray());
    }

    QSocketNotifier m_input;
#endif

    // What came from the program; nothing: it closed the pipe (or is gone).
    void take(const QByteArray &data)
    {
        if (data.isEmpty()) {
            QCoreApplication::quit();
            return;
        }
        m_buffer.append(data);
        qsizetype end;
        while ((end = m_buffer.indexOf('\n')) >= 0) {
            const QJsonObject message = QJsonDocument::fromJson(m_buffer.left(end)).object();
            m_buffer.remove(0, end + 1);
            m_handle(message);
        }
        if (m_afterAll)
            m_afterAll();
    }

    QByteArray m_buffer;
    std::function<void(const QJsonObject &)> m_handle;
    std::function<void()> m_afterAll;
};
