#include "MailStore.h"
#include "MailWebView.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>

#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    // Answered before anything starts, so they work without a display (the package's install check uses them).
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::printf("argonaut-emails %s\n", MAIL_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("Usage: argonaut-emails [--version] [--help]\n\n"
                        "Argonaut Emails, an email client (IMAP and SMTP) in Qt.\n");
            return 0;
        }
    }
    MailWebView::prepare(); // before the application
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Argonaut"));
    QApplication::setApplicationName(QStringLiteral("argonaut-emails"));
    QApplication::setApplicationDisplayName(QStringLiteral("Argonaut Emails"));
    QApplication::setApplicationVersion(QStringLiteral(MAIL_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("argonaut-emails"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    applyThemeKey(savedThemeKey());

    MailStore store;
    if (!store.isOpen())
        qWarning("The mail store couldn't be opened: %s", qPrintable(store.error()));
    MainWindow window(&store);
    window.show();
    return app.exec();
}
