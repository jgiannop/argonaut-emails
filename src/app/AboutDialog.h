#pragma once

#include <QDialog>

class QStackedWidget;

// Help → About Argonaut Emails, as Argonaut's but brighter: a banner in the
// theme's accent (the icon on a glow, the name, the version on a pill, the
// tagline, envelopes sailing past), then three pages: About (what it is, four
// cards), System (versions and folders, Copy Details for bug reports) and
// Credits.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

    // The System page as plain text, for "Copy Details".
    static QString systemDetails();

    QStackedWidget *pages() const { return m_pages; }

private:
    QWidget *buildAboutPage();
    QWidget *buildSystemPage();
    QWidget *buildCreditsPage();

    QStackedWidget *m_pages = nullptr;
};
