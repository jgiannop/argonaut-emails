#pragma once
// A fake Claude API for tests: a one-request-at-a-time HTTP server on
// localhost that answers from a queue and records every request.

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

class FakeApi : public QObject
{
public:
    struct Reply
    {
        int status;
        QByteArray body;
        QByteArray extraHeaders;
    };
    struct Request
    {
        QByteArray method;
        QByteArray path;
        QHash<QByteArray, QByteArray> headers;
        QJsonObject body;
    };

    QList<Reply> replies;
    QList<Request> requests;

    FakeApi()
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, this, [this, s] { handle(s); });
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

private:
    void handle(QTcpSocket *s)
    {
        QByteArray &buf = m_buffers[s];
        buf += s->readAll();
        const qsizetype end = buf.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        Request r;
        const QList<QByteArray> lines = buf.left(end).split('\n');
        const QList<QByteArray> first = lines.first().trimmed().split(' ');
        r.method = first.value(0);
        r.path = first.value(1);
        qsizetype length = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines[i].indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray name = lines[i].left(colon).trimmed().toLower();
            const QByteArray value = lines[i].mid(colon + 1).trimmed();
            r.headers.insert(name, value);
            if (name == "content-length")
                length = value.toLongLong();
        }
        if (buf.size() < end + 4 + length)
            return; // body not complete yet
        r.body = QJsonDocument::fromJson(buf.mid(end + 4, length)).object();
        buf.clear();
        requests.append(r);
        const Reply reply = replies.isEmpty() ? Reply{500, "{}", {}} : replies.takeFirst();
        QByteArray out = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\nContent-Type: application/json\r\n"
                         + reply.extraHeaders + "Content-Length: " + QByteArray::number(reply.body.size())
                         + "\r\nConnection: close\r\n\r\n" + reply.body;
        s->write(out);
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

