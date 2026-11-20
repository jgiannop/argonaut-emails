#pragma once

#include <QString>

// A message's HTML made fit to show in the message view (Chromium, through
// Qt WebEngine, with JavaScript off, a Content-Security-Policy, and every
// network request refused). This is the first of those locks: what has no
// business in a message is taken out: scripts, the title, frames, objects,
// forms, comments and every on… attribute; links only http(s), mailto, #.
// What makes its look stays: <style>, style attributes, the layout tables.
// Pictures stay where they are (cid: ones are the message's own; data:
// images are in it); those from the internet are counted, and the view only
// loads them when asked (they tell the sender when and where it was read).
namespace SafeHtml {

struct Result
{
    QString html;
    int remotePictures = 0; // <img> and CSS url() from the internet
};

Result clean(const QString &html);
// Plain text as HTML: escaped, links made clickable, quoted lines dimmed.
QString fromPlainText(const QString &text);

} // namespace SafeHtml
