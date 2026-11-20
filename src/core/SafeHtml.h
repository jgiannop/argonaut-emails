#pragma once

#include <QString>

// A message's HTML made fit to show in Qt's rich-text view, which runs no
// scripts and loads nothing by itself (MessageView refuses every resource
// but the message's own cid: pictures). What it can't use is taken out
// first: scripts, styles, the head, frames, objects, forms, comments and
// every on… attribute. Pictures from the internet are not loaded (they
// tell the sender when and where the message was read); they are counted,
// and their place keeps their alt text.
namespace SafeHtml {

struct Result
{
    QString html;
    int remotePictures = 0;
};

Result clean(const QString &html);
// Plain text as HTML: escaped, links made clickable, quoted lines dimmed.
QString fromPlainText(const QString &text);

} // namespace SafeHtml
