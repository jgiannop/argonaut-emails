#include "Claude.h"
#include "ClaudeReports.h"

#include "Passwords.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace {

constexpr const char *kApiVersion = "2026-10-21";
constexpr const char *kFallbackBeta = "server-side-fallback-2026-07-01";
constexpr int kMaxTokens = 16000; // non-streaming: keeps responses under HTTP timeouts

} // namespace

namespace Claude {

// ---------------------------------------------------------------- settings

const QList<ModelInfo> &models()
{
    // Prices per million tokens (Haiku: prompts up to 100K tokens).
    static const QList<ModelInfo> list = {
        {QStringLiteral("claude-opus-5-5"), QStringLiteral("Claude Opus 5.5"), 4.0, 20.0, true},
        {QStringLiteral("claude-sonnet-5-5"), QStringLiteral("Claude Sonnet 5.5"), 2.0, 10.0, true},
        {QStringLiteral("claude-haiku-5-5"), QStringLiteral("Claude Haiku 5.5"), 0.10, 0.50, false},
    };
    return list;
}

ModelInfo modelInfo(const QString &id)
{
    for (const ModelInfo &m : models()) {
        if (m.id == id)
            return m;
    }
    return models().first();
}

QString model()
{
    return modelInfo(QSettings().value(QStringLiteral("claude/model")).toString()).id;
}

void setModel(const QString &id)
{
    QSettings().setValue(QStringLiteral("claude/model"), modelInfo(id).id);
}

double cost(const QString &modelId, qint64 inputTokens, qint64 outputTokens)
{
    const ModelInfo m = modelInfo(modelId);
    double in = m.inputPerMTok;
    double out = m.outputPerMTok;
    if (m.id == QLatin1String("claude-haiku-5-5") && inputTokens > 100000) {
        in = 0.50; // longer prompts cost more on Haiku
        out = 2.50;
    }
    return (double(inputTokens) * in + double(outputTokens) * out) / 1e6;
}

double spent()
{
    return QSettings().value(QStringLiteral("claude/spent"), 0.0).toDouble();
}

void addSpent(double dollars)
{
    QSettings().setValue(QStringLiteral("claude/spent"), spent() + dollars);
}

void resetSpent()
{
    QSettings().setValue(QStringLiteral("claude/spent"), 0.0);
}

// ------------------------------------------------------------------ client

Client::Client(QObject *parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString storedKey(bool *fromEnvironment)
{
    const QString env = qEnvironmentVariable("ANTHROPIC_API_KEY").trimmed();
    if (fromEnvironment)
        *fromEnvironment = !env.isEmpty();
    if (!env.isEmpty())
        return env;
    return Passwords::load(QStringLiteral("claude"), QStringLiteral("anthropic-api-key")).trimmed();
}

bool saveKey(const QString &key, QString *error)
{
    return Passwords::save(QStringLiteral("claude"), QStringLiteral("anthropic-api-key"), key.trimmed(),
                           QStringLiteral("Anthropic API key"), nullptr, error);
}

bool removeKey()
{
    return Passwords::remove(QStringLiteral("claude"), QStringLiteral("anthropic-api-key"));
}

QString Client::apiKey() const
{
    return m_key.isEmpty() ? storedKey() : m_key;
}

QString Client::baseUrl()
{
    const QString overridden = qEnvironmentVariable("MAIL_CLAUDE_BASE_URL");
    return overridden.isEmpty() ? QStringLiteral("https://api.anthropic.com") : overridden;
}

Error Client::parseError(int status, const QByteArray &body, const QString &networkError)
{
    Error e;
    e.status = status;
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonObject err = root.value(QStringLiteral("error")).toObject();
    e.type = err.value(QStringLiteral("type")).toString();
    e.requestId = root.value(QStringLiteral("request_id")).toString();
    const QString detail = err.value(QStringLiteral("message")).toString();
    e.retryable = status == 429 || status == 500 || status == 529 || status == 503 || status == 502 || status == 0;
    switch (status) {
    case 0:
        e.message = QObject::tr("Couldn't reach the Claude API (%1)").arg(networkError);
        break;
    case 401:
        e.message = QObject::tr("The API key isn't valid. Check it in Edit → Claude.");
        break;
    case 402:
        e.message = QObject::tr("Billing problem on your Anthropic account: %1").arg(detail);
        break;
    case 403:
        e.message = QObject::tr("This API key isn't allowed to do that: %1").arg(detail);
        break;
    case 404:
        e.message = QObject::tr("Not available for this account: %1").arg(detail);
        break;
    case 413:
        e.message = QObject::tr("The request is too large for Claude.");
        break;
    case 429:
        e.message = QObject::tr("Too many requests for now. Try again in a minute.");
        break;
    case 529:
        e.message = QObject::tr("Claude is overloaded right now. Try again shortly.");
        break;
    default:
        e.message = detail.isEmpty() ? QObject::tr("The Claude API answered with error %1").arg(status) : detail;
        break;
    }
    return e;
}

QNetworkReply *Client::post(const QString &path, const QJsonObject &body, bool beta)
{
    QNetworkRequest request(QUrl(baseUrl() + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", apiKey().toUtf8());
    request.setRawHeader("anthropic-version", kApiVersion);
    if (beta)
        request.setRawHeader("anthropic-beta", kFallbackBeta);
    request.setTransferTimeout(10 * 60 * 1000);
    return m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void Client::testKey(const QString &key, std::function<void(bool, const QString &)> done)
{
    QNetworkRequest request(QUrl(baseUrl() + QStringLiteral("/v1/models?limit=1")));
    request.setRawHeader("x-api-key", key.trimmed().toUtf8());
    request.setRawHeader("anthropic-version", kApiVersion);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            done(true, QObject::tr("The key works."));
            return;
        }
        const Error e = parseError(status, reply->readAll(),
                                   reply->error() == QNetworkReply::NoError ? QString() : reply->errorString());
        done(false, e.message);
    });
}

void Client::countTokens(QJsonObject request, std::function<void(qint64, const Error &)> done)
{
    if (!request.contains(QStringLiteral("model")))
        request.insert(QStringLiteral("model"), model());
    request.remove(QStringLiteral("max_tokens"));
    request.remove(QStringLiteral("fallbacks"));
    QNetworkReply *reply = post(QStringLiteral("/v1/messages/count_tokens"), request, false);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (status == 200) {
            done(QJsonDocument::fromJson(body).object().value(QStringLiteral("input_tokens")).toInteger(), Error());
            return;
        }
        done(0, parseError(status, body, reply->error() == QNetworkReply::NoError ? QString() : reply->errorString()));
    });
}

void Client::send(QJsonObject request, std::function<void(const Response &, const Error &)> done)
{
    if (!request.contains(QStringLiteral("model")))
        request.insert(QStringLiteral("model"), model());
    if (!request.contains(QStringLiteral("max_tokens")))
        request.insert(QStringLiteral("max_tokens"), kMaxTokens);
    // If a safety filter declines, the API retries on another model itself.
    if (modelInfo(request.value(QStringLiteral("model")).toString()).fallbacks && !request.contains(QStringLiteral("fallbacks")))
        request.insert(QStringLiteral("fallbacks"), QStringLiteral("default"));
    // Every answer is kept as a page the user can read later, with what was
    // sent (ClaudeReports): here, so that nothing that asks Claude can forget to.
    const QString askedBy = m_askedBy;
    sendAttempt(request, 0, [request, askedBy, done = std::move(done)](const Response &response, const Error &error) {
        if (!error.message.isEmpty() || response.message.isEmpty()) {
            done(response, error);
            return;
        }
        Response kept = response;
        kept.reportFile = ClaudeReports::save(askedBy, request, response);
        done(kept, error);
    });
}

void Client::sendAttempt(QJsonObject request, int attempt, std::function<void(const Response &, const Error &)> done)
{
    const bool beta = request.contains(QStringLiteral("fallbacks"));
    QNetworkReply *reply = post(QStringLiteral("/v1/messages"), request, beta);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, attempt, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (status != 200) {
            Error e = parseError(status, body, reply->error() == QNetworkReply::NoError ? QString() : reply->errorString());
            if (e.retryable && attempt < 2) {
                // Wait as asked (retry-after), or 1 s, then 4 s.
                int wait = attempt == 0 ? 1000 : 4000;
                const QByteArray after = reply->rawHeader("retry-after");
                if (!after.isEmpty())
                    wait = qBound(1000, after.toInt() * 1000, 60000);
                QTimer::singleShot(wait, this, [this, request, attempt, done] { sendAttempt(request, attempt + 1, done); });
                return;
            }
            done(Response(), e);
            return;
        }
        Response r;
        r.message = QJsonDocument::fromJson(body).object();
        r.stopReason = r.message.value(QStringLiteral("stop_reason")).toString();
        const QJsonObject usage = r.message.value(QStringLiteral("usage")).toObject();
        r.inputTokens = usage.value(QStringLiteral("input_tokens")).toInteger()
                        + usage.value(QStringLiteral("cache_creation_input_tokens")).toInteger()
                        + usage.value(QStringLiteral("cache_read_input_tokens")).toInteger();
        r.outputTokens = usage.value(QStringLiteral("output_tokens")).toInteger();
        const QString servedBy = r.message.value(QStringLiteral("model")).toString(request.value(QStringLiteral("model")).toString());
        r.dollars = cost(servedBy, r.inputTokens, r.outputTokens);
        addSpent(r.dollars);
        QStringList texts;
        for (const QJsonValue &block : r.message.value(QStringLiteral("content")).toArray()) {
            const QJsonObject b = block.toObject();
            if (b.value(QStringLiteral("type")).toString() == QLatin1String("text"))
                texts << b.value(QStringLiteral("text")).toString();
        }
        r.text = texts.join(QString());
        if (r.stopReason == QLatin1String("refusal")) {
            Error e;
            e.status = 200;
            e.type = QStringLiteral("refusal");
            e.message = QObject::tr("Claude declined this request.");
            done(r, e);
            return;
        }
        done(r, Error());
    });
}

} // namespace Claude
