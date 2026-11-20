#pragma once

// Pretend IMAP and SMTP servers on 127.0.0.1, enough of each to test the
// clients and the sync against: they log what they were asked.

#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

class FakeImap : public QObject
{
public:
    struct Mail
    {
        qint64 uid = 0;
        QStringList flags;
        QByteArray raw;
        QByteArray category; // Gmail's inbox tab ("social"…), for X-GM-RAW
    };
    struct Box
    {
        QList<Mail> mails;
        qint64 uidNext = 1;
        qint64 uidValidity = 7;
        QByteArray role; // "\\Sent", "\\Trash"…
    };

    QMap<QByteArray, Box> boxes;
    QString user = QStringLiteral("maria@example.com");
    QString password = QStringLiteral("σωστό-pass");
    QByteArray capabilities = "IMAP4rev1 AUTH=PLAIN SASL-IR MOVE UIDPLUS";
    QByteArray beforeLogin; // like Gmail: fewer capabilities until logged in (empty: the same)
    bool capabilityInLoginAnswer = true;
    QList<QByteArray> log; // every command line, tag removed

    FakeImap()
    {
        boxes["INBOX"];
        boxes["Sent"].role = "\\Sent";
        boxes["Trash"].role = "\\Trash";
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                s->setParent(this);
                s->write("* OK fake IMAP ready\r\n");
                connect(s, &QTcpSocket::readyRead, this, [this, s] { onRead(s); });
            }
        });
        m_server.listen(QHostAddress::LocalHost);
    }
    ~FakeImap() override
    {
        // The connections first: what they read is about to go.
        const QList<QTcpSocket *> sockets = findChildren<QTcpSocket *>(QString(), Qt::FindDirectChildrenOnly);
        for (QTcpSocket *s : sockets) {
            s->disconnect(this);
            delete s;
        }
    }
    quint16 port() const { return m_server.serverPort(); }

    qint64 add(const QByteArray &box, const QByteArray &raw, const QStringList &flags = {})
    {
        Box &b = boxes[box];
        Mail m;
        m.uid = b.uidNext++;
        m.raw = raw;
        m.flags = flags;
        b.mails.append(m);
        return m.uid;
    }
    Mail *find(const QByteArray &box, qint64 uid)
    {
        for (Mail &m : boxes[box].mails) {
            if (m.uid == uid)
                return &m;
        }
        return nullptr;
    }
    bool loggedCommand(const QByteArray &start) const
    {
        for (const QByteArray &l : log) {
            if (l.startsWith(start))
                return true;
        }
        return false;
    }

private:
    struct Conn
    {
        QByteArray buffer;
        QByteArray selected;
        bool authed = false;
        // An APPEND waiting for its literal.
        QByteArray appendTag;
        QByteArray appendBox;
        qsizetype appendSize = -1;
    };

    void onRead(QTcpSocket *s)
    {
        Conn &c = m_conns[s];
        c.buffer += s->readAll();
        while (true) {
            if (c.appendSize >= 0) {
                if (c.buffer.size() < c.appendSize + 2)
                    return;
                add(c.appendBox, c.buffer.left(c.appendSize), {QStringLiteral("\\Seen")});
                c.buffer.remove(0, c.appendSize + 2);
                c.appendSize = -1;
                s->write(c.appendTag + " OK [APPENDUID 7 1] done\r\n");
                continue;
            }
            const qsizetype nl = c.buffer.indexOf("\r\n");
            if (nl < 0)
                return;
            const QByteArray line = c.buffer.left(nl);
            c.buffer.remove(0, nl + 2);
            command(s, c, line);
        }
    }

    static QByteArray unquote(QByteArray v)
    {
        if (v.startsWith('"') && v.endsWith('"') && v.size() >= 2)
            v = v.mid(1, v.size() - 2);
        return v.replace("\\\"", "\"").replace("\\\\", "\\");
    }

    // "1:5,8" against the highest number there is.
    static QList<qint64> numbers(const QByteArray &set, qint64 highest)
    {
        QList<qint64> out;
        for (const QByteArray &piece : set.split(',')) {
            const QList<QByteArray> ends = piece.split(':');
            qint64 a = ends.at(0) == "*" ? highest : ends.at(0).toLongLong();
            qint64 b = ends.size() > 1 ? (ends.at(1) == "*" ? highest : ends.at(1).toLongLong()) : a;
            if (a > b)
                std::swap(a, b);
            for (qint64 n = a; n <= b; ++n)
                out.append(n);
        }
        return out;
    }

    QByteArray fetchData(const Mail &m, int seq, const QByteArray &items)
    {
        QByteArray d = "* " + QByteArray::number(seq) + " FETCH (UID " + QByteArray::number(m.uid) + " FLAGS (" + m.flags.join(' ').toUtf8() + ')';
        if (items.contains("RFC822.SIZE"))
            d += " RFC822.SIZE " + QByteArray::number(m.raw.size());
        if (items.contains("INTERNALDATE"))
            d += " INTERNALDATE \"17-Jul-2026 02:44:25 +0300\"";
        if (items.contains("BODY.PEEK[HEADER")) {
            qsizetype end = m.raw.indexOf("\r\n\r\n");
            const QByteArray headers = end < 0 ? m.raw : m.raw.left(end + 4);
            d += " BODY[HEADER.FIELDS (FROM TO CC SUBJECT DATE MESSAGE-ID CONTENT-TYPE)] {" + QByteArray::number(headers.size()) + "}\r\n" + headers;
        } else if (items.contains("BODY.PEEK[]")) {
            d += " BODY[] {" + QByteArray::number(m.raw.size()) + "}\r\n" + m.raw;
        }
        return d + ")\r\n";
    }

    void command(QTcpSocket *s, Conn &c, const QByteArray &line)
    {
        const qsizetype sp = line.indexOf(' ');
        const QByteArray tag = line.left(sp);
        const QByteArray rest = line.mid(sp + 1);
        log.append(rest);
        const QList<QByteArray> w = rest.split(' ');
        const QByteArray cmd = w.value(0).toUpper();
        auto ok = [&](const QByteArray &text = "done") { s->write(tag + " OK " + text + "\r\n"); };
        auto no = [&](const QByteArray &text) { s->write(tag + " NO " + text + "\r\n"); };
        if (cmd == "CAPABILITY") {
            s->write("* CAPABILITY " + (c.authed || beforeLogin.isEmpty() ? capabilities : beforeLogin) + "\r\n");
            ok();
        } else if (cmd == "AUTHENTICATE" && w.value(1).toUpper() == "PLAIN") {
            const QList<QByteArray> parts = QByteArray::fromBase64(w.value(2)).split('\0');
            if (parts.size() == 3 && QString::fromUtf8(parts.at(1)) == user && QString::fromUtf8(parts.at(2)) == password) {
                c.authed = true;
                ok(capabilityInLoginAnswer ? "[CAPABILITY " + capabilities + "] logged in" : QByteArray("logged in"));
            } else {
                no("[AUTHENTICATIONFAILED] Invalid credentials");
            }
        } else if (cmd == "LOGIN") {
            if (QString::fromUtf8(unquote(w.value(1))) == user && QString::fromUtf8(unquote(w.value(2))) == password) {
                c.authed = true;
                ok();
            } else {
                no("[AUTHENTICATIONFAILED] Invalid credentials");
            }
        } else if (!c.authed) {
            s->write(tag + " BAD log in first\r\n");
        } else if (cmd == "LIST") {
            for (auto it = boxes.cbegin(); it != boxes.cend(); ++it)
                s->write("* LIST (\\HasNoChildren" + (it->role.isEmpty() ? QByteArray() : ' ' + it->role) + ") \"/\" \"" + it.key() + "\"\r\n");
            ok();
        } else if (cmd == "SELECT") {
            const QByteArray name = unquote(rest.mid(7));
            if (!boxes.contains(name)) {
                no("no such folder");
                return;
            }
            c.selected = name;
            const Box &b = boxes[name];
            s->write("* " + QByteArray::number(b.mails.size()) + " EXISTS\r\n");
            s->write("* OK [UIDVALIDITY " + QByteArray::number(b.uidValidity) + "] ok\r\n");
            s->write("* OK [UIDNEXT " + QByteArray::number(b.uidNext) + "] ok\r\n");
            ok("[READ-WRITE] selected");
        } else if (cmd == "FETCH") {
            const Box &b = boxes[c.selected];
            for (qint64 n : numbers(w.value(1), b.mails.size())) {
                if (n >= 1 && n <= b.mails.size())
                    s->write(fetchData(b.mails.at(n - 1), int(n), rest));
            }
            ok();
        } else if (cmd == "UID" && w.value(1).toUpper() == "FETCH") {
            const Box &b = boxes[c.selected];
            const qint64 highest = b.mails.isEmpty() ? 0 : b.mails.last().uid;
            const QList<qint64> uids = numbers(w.value(2), highest);
            for (int i = 0; i < b.mails.size(); ++i) {
                if (uids.contains(b.mails.at(i).uid))
                    s->write(fetchData(b.mails.at(i), i + 1, rest));
            }
            ok();
        } else if (cmd == "UID" && w.value(1).toUpper() == "SEARCH" && w.value(2).toUpper() == "X-GM-RAW") {
            // Only what the client asks: "category:<name>".
            const QByteArray query = unquote(rest.mid(rest.toUpper().indexOf("X-GM-RAW") + 9));
            if (!capabilities.contains("X-GM-EXT-1") || !query.startsWith("category:")) {
                s->write(tag + " BAD not here\r\n");
                return;
            }
            QByteArray found = "* SEARCH";
            for (const Mail &m : boxes.value(c.selected).mails) {
                if (m.category == query.mid(9))
                    found += ' ' + QByteArray::number(m.uid);
            }
            s->write(found + "\r\n");
            ok();
        } else if (cmd == "UID" && w.value(1).toUpper() == "STORE") {
            Box &b = boxes[c.selected];
            const qint64 uid = w.value(2).toLongLong();
            const bool add = w.value(3).startsWith('+');
            QByteArray flags = rest.mid(rest.indexOf('(') + 1);
            flags.chop(1);
            for (Mail &m : b.mails) {
                if (m.uid != uid)
                    continue;
                for (const QByteArray &f : flags.split(' ')) {
                    m.flags.removeAll(QString::fromUtf8(f));
                    if (add)
                        m.flags.append(QString::fromUtf8(f));
                }
            }
            ok();
        } else if (cmd == "UID" && w.value(1).toUpper() == "MOVE") {
            Box &from = boxes[c.selected];
            const qint64 uid = w.value(2).toLongLong();
            const QByteArray to = unquote(w.value(3));
            for (int i = 0; i < from.mails.size(); ++i) {
                if (from.mails.at(i).uid == uid) {
                    const Mail m = from.mails.takeAt(i);
                    add(to, m.raw, m.flags);
                    s->write("* " + QByteArray::number(i + 1) + " EXPUNGE\r\n");
                    break;
                }
            }
            ok();
        } else if (cmd == "APPEND") {
            const qsizetype open = rest.lastIndexOf('{');
            c.appendTag = tag;
            c.appendBox = unquote(w.value(1));
            c.appendSize = rest.mid(open + 1, rest.size() - open - 2).toLongLong();
            s->write("+ go on\r\n");
        } else if (cmd == "LOGOUT") {
            s->write("* BYE bye\r\n");
            ok();
            s->disconnectFromHost();
        } else {
            s->write(tag + " BAD unknown\r\n");
        }
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, Conn> m_conns;
};

class FakeSmtp : public QObject
{
public:
    QString user = QStringLiteral("maria@example.com");
    QString password = QStringLiteral("σωστό-pass");
    QByteArray from;
    QList<QByteArray> to;
    QByteArray data; // as it came, dot-stuffing and all
    QList<QByteArray> log;
    bool refuseRecipient = false;

    FakeSmtp()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                s->setParent(this);
                s->write("220 fake ESMTP\r\n");
                connect(s, &QTcpSocket::readyRead, this, [this, s] { onRead(s); });
            }
        });
        m_server.listen(QHostAddress::LocalHost);
    }
    ~FakeSmtp() override
    {
        const QList<QTcpSocket *> sockets = findChildren<QTcpSocket *>(QString(), Qt::FindDirectChildrenOnly);
        for (QTcpSocket *s : sockets) {
            s->disconnect(this);
            delete s;
        }
    }
    quint16 port() const { return m_server.serverPort(); }

private:
    void onRead(QTcpSocket *s)
    {
        m_buffer += s->readAll();
        while (true) {
            if (m_inData) {
                const qsizetype end = m_buffer.indexOf("\r\n.\r\n");
                if (end < 0)
                    return;
                data = m_buffer.left(end + 2);
                m_buffer.remove(0, end + 5);
                m_inData = false;
                s->write("250 queued\r\n");
                continue;
            }
            const qsizetype nl = m_buffer.indexOf("\r\n");
            if (nl < 0)
                return;
            const QByteArray line = m_buffer.left(nl);
            m_buffer.remove(0, nl + 2);
            log.append(line);
            if (line.startsWith("EHLO")) {
                s->write("250-fake hello\r\n250-AUTH PLAIN LOGIN\r\n250 8BITMIME\r\n");
            } else if (line.startsWith("AUTH PLAIN ")) {
                const QList<QByteArray> parts = QByteArray::fromBase64(line.mid(11)).split('\0');
                const bool good = parts.size() == 3 && QString::fromUtf8(parts.at(1)) == user && QString::fromUtf8(parts.at(2)) == password;
                s->write(good ? "235 ok\r\n" : "535 bad credentials\r\n");
            } else if (line.startsWith("MAIL FROM:")) {
                from = line.mid(10);
                s->write("250 ok\r\n");
            } else if (line.startsWith("RCPT TO:")) {
                to.append(line.mid(8));
                s->write(refuseRecipient ? "550 no such user\r\n" : "250 ok\r\n");
            } else if (line == "DATA") {
                m_inData = true;
                s->write("354 go on\r\n");
            } else if (line == "QUIT") {
                s->write("221 bye\r\n");
                s->disconnectFromHost();
            } else {
                s->write("500 what\r\n");
            }
        }
    }

    QTcpServer m_server;
    QByteArray m_buffer;
    bool m_inData = false;
};
