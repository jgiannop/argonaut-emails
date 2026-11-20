#pragma once

#include "Mime.h"

#include <QColor>
#include <QWebEngineView>

class QWebEngineProfile;

// A message as Gmail would show it (Chromium, through Qt WebEngine), locked
// three times so that it can only be looked at:
//   1. JavaScript, plugins, storage, link pings, DNS prefetch: off, in a
//      profile of its own that keeps nothing (no cookies, no cache on disk);
//   2. a Content-Security-Policy on every page: no scripts, frames, forms,
//      fonts or style sheets from elsewhere; pictures only the message's own,
//      and the internet's only after "Show pictures";
//   3. every request the page makes is refused, but the message's own parts
//      and, after "Show pictures", pictures over http(s).
// The page comes from argonaut-mail:/message/<n>, its pictures from
// argonaut-mail:/cid/<content-id>. Links never open in it: they are handed
// to the window (linkClicked), which asks first.
class MailWebView : public QWebEngineView
{
    Q_OBJECT

public:
    // Before the QApplication: the scheme, and the shared GL contexts WebEngine wants.
    static void prepare();

    explicit MailWebView(QWidget *parent = nullptr);
    ~MailWebView() override;

    // Plain text and notes in the theme's colours (a designed message stays on white).
    void setColors(const QColor &background, const QColor &text);
    // `html` from SafeHtml (or fromPlainText); `message` keeps the cid: parts (kept alive by the caller).
    void showHtml(const QString &html, const Mime::Message *message, bool remotePictures, bool plain);
    void showNote(const QString &text);
    // What is shown, as text, once the page is there (empty before): for tests.
    QString shownText() const { return m_text; }
    bool remotePicturesAllowed() const;
    // WebEngine's own is next to nothing: the splitter would give the message no room.
    QSize sizeHint() const override { return {600, 400}; }

signals:
    void linkClicked(const QUrl &url);
    void shown(); // the page is there (its text too)

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    class Handler;
    class Interceptor;
    class Page;

    void load(const QByteArray &document);

    QWebEngineProfile *m_profile = nullptr;
    Page *m_page = nullptr;
    Handler *m_handler = nullptr;
    Interceptor *m_interceptor = nullptr;
    quint64 m_serial = 0;
    QString m_text;
    QColor m_background = Qt::white;
    QColor m_foreground = Qt::black;
};
