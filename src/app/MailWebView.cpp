#include "MailWebView.h"

#include <QBuffer>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QMenu>
#include <QRegularExpression>
#include <QWebEngineContextMenuRequest>
#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>

#include <atomic>
#include <functional>

namespace {

// A constant, not a QByteArray: prepare() may run before this file's statics are made.
constexpr char kScheme[] = "argonaut-mail";

bool isOurs(const QUrl &url)
{
    return url.scheme() == QLatin1String(kScheme);
}

} // namespace

// The message and its own pictures, from memory: nothing else is served.
class MailWebView::Handler : public QWebEngineUrlSchemeHandler
{
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;

    QByteArray document;
    quint64 serial = 0;
    const Mime::Message *message = nullptr;

    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        const QString path = job->requestUrl().path(QUrl::FullyEncoded);
        if (path == QStringLiteral("/message/%1").arg(serial)) {
            reply(job, "text/html;charset=utf-8", document);
            return;
        }
        if (path.startsWith(QLatin1String("/cid/")) && message) {
            const QByteArray id = QByteArray::fromPercentEncoding(path.mid(5).toLatin1());
            const Mime::Part *part = message->partById(id);
            if (part && part->type.startsWith("image/")) {
                reply(job, part->type, part->body);
                return;
            }
        }
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    }

private:
    static void reply(QWebEngineUrlRequestJob *job, const QByteArray &type, const QByteArray &data)
    {
        auto *buffer = new QBuffer(job);
        buffer->setData(data);
        buffer->open(QIODevice::ReadOnly);
        job->reply(type, buffer);
    }
};

// Every request but ours is refused; pictures over http(s) only when allowed.
class MailWebView::Interceptor : public QWebEngineUrlRequestInterceptor
{
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;

    std::atomic<bool> remote{false};

    void interceptRequest(QWebEngineUrlRequestInfo &info) override
    {
        const QUrl url = info.requestUrl();
        const QString scheme = url.scheme();
        if (isOurs(url) || scheme == QLatin1String("data"))
            return;
        if (remote && (scheme == QLatin1String("https") || scheme == QLatin1String("http"))
            && info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeImage)
            return;
        info.block(true);
    }
};

// Only our own pages load in it; a clicked link goes to the window, which asks first.
class MailWebView::Page : public QWebEnginePage
{
public:
    Page(QWebEngineProfile *profile, QObject *parent, std::function<void(const QUrl &)> onLink)
        : QWebEnginePage(profile, parent)
        , m_onLink(std::move(onLink))
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        if (!isMainFrame)
            return false;
        // A link to a place in the message itself ("#top").
        if (url.hasFragment() && url.adjusted(QUrl::RemoveFragment) == this->url().adjusted(QUrl::RemoveFragment))
            return true;
        if (type == NavigationTypeLinkClicked) {
            m_onLink(url);
            return false;
        }
        return isOurs(url) && type != NavigationTypeFormSubmitted && type != NavigationTypeRedirect;
    }

    // What the page says of what it refused (the policy at work) stays quiet.
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString &, int, const QString &) override {}

private:
    std::function<void(const QUrl &)> m_onLink;
};

void MailWebView::prepare()
{
    QWebEngineUrlScheme scheme{QByteArray(kScheme)};
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme); // https pictures in it aren't "mixed content"
    QWebEngineUrlScheme::registerScheme(scheme);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
}

MailWebView::MailWebView(QWidget *parent)
    : QWebEngineView(parent)
{
    // No storage name: "off the record", nothing written to the disk.
    m_profile = new QWebEngineProfile(this);
    m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    m_profile->setSpellCheckEnabled(false);
    m_handler = new Handler(this);
    m_profile->installUrlSchemeHandler(QByteArray(kScheme), m_handler);
    m_interceptor = new Interceptor(this);
    m_profile->setUrlRequestInterceptor(m_interceptor);
    connect(m_profile, &QWebEngineProfile::downloadRequested, this, [](QWebEngineDownloadRequest *download) { download->cancel(); });

    m_page = new Page(m_profile, this, [this](const QUrl &url) { emit linkClicked(url); });
    QWebEngineSettings *s = m_page->settings();
    s->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
    s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    s->setAttribute(QWebEngineSettings::LocalStorageEnabled, false);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    s->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
    s->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    s->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, false);
    s->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
    s->setAttribute(QWebEngineSettings::WebGLEnabled, false);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
    s->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, false);
    s->setAttribute(QWebEngineSettings::NavigateOnDropEnabled, false);
    s->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);
    s->setAttribute(QWebEngineSettings::AutoLoadIconsForPage, false);
    s->setAttribute(QWebEngineSettings::TouchIconsEnabled, false);
    setPage(m_page);
    connect(m_page, &QWebEnginePage::loadFinished, this, [this] {
        const quint64 serial = m_serial;
        m_page->toPlainText([this, serial](const QString &text) {
            if (serial != m_serial)
                return; // another message came since
            m_text = text;
            emit shown();
        });
    });
}

MailWebView::~MailWebView()
{
    // The page before its profile (WebEngine insists).
    delete m_page;
    m_page = nullptr;
}

bool MailWebView::remotePicturesAllowed() const
{
    return m_interceptor->remote;
}

void MailWebView::setColors(const QColor &background, const QColor &text)
{
    m_background = background;
    m_foreground = text;
}

void MailWebView::showHtml(const QString &html, const Mime::Message *message, bool remotePictures, bool plain)
{
    m_handler->message = message;
    m_interceptor->remote = remotePictures;
    // The message's own pictures come from us.
    static const QRegularExpression cid(QStringLiteral("(\\s(?:src|background)=\")cid:"), QRegularExpression::CaseInsensitiveOption);
    QString body = html;
    body.replace(cid, QStringLiteral("\\1") + QString::fromLatin1(kScheme) + QStringLiteral(":/cid/"));

    const QString pictures = remotePictures ? QStringLiteral(" http: https:") : QString();
    // A designed message (HTML) on white, as its sender made it, as Gmail shows it; plain text in the theme's colours.
    const QString colors = plain ? QStringLiteral("html{background:%1;color:%2}").arg(m_background.name(), m_foreground.name())
                                 : QStringLiteral("html{background:#ffffff;color:#202124}");
    const QString document =
        QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                       "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; "
                       "img-src %1: data:%2; form-action 'none'; base-uri 'none'\">"
                       "<meta name=\"referrer\" content=\"no-referrer\">"
                       "<style>%3 body{margin:16px;font-family:Arial,Helvetica,sans-serif;font-size:14px;overflow-wrap:break-word}"
                       " a{color:#1a73e8}</style></head><body>")
            .arg(QString::fromLatin1(kScheme), pictures, colors)
        + body + QStringLiteral("</body></html>");
    load(document.toUtf8());
}

void MailWebView::showNote(const QString &text)
{
    m_handler->message = nullptr;
    m_interceptor->remote = false;
    const QString document =
        QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                       "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'\">"
                       "<style>html{background:%1;color:%2;font-family:sans-serif;font-size:14px}"
                       " p{text-align:center;margin-top:3em;opacity:.6}</style></head><body><p>%3</p></body></html>")
            .arg(m_background.name(), m_foreground.name(), text.toHtmlEscaped());
    load(document.toUtf8());
}

void MailWebView::load(const QByteArray &document)
{
    ++m_serial;
    m_text.clear();
    m_handler->document = document;
    m_handler->serial = m_serial;
    m_page->setUrl(QUrl(QStringLiteral("%1:/message/%2").arg(QString::fromLatin1(kScheme)).arg(m_serial)));
}

void MailWebView::contextMenuEvent(QContextMenuEvent *event)
{
    // Copy, select; nothing that would open, save or reload the page.
    QMenu menu(this);
    const QWebEngineContextMenuRequest *request = lastContextMenuRequest();
    if (request && !request->selectedText().isEmpty())
        menu.addAction(m_page->action(QWebEnginePage::Copy));
    if (request && request->linkUrl().isValid() && !isOurs(request->linkUrl()))
        menu.addAction(m_page->action(QWebEnginePage::CopyLinkToClipboard));
    menu.addAction(m_page->action(QWebEnginePage::SelectAll));
    menu.exec(event->globalPos());
}
