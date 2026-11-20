#include "MailStore.h"
#include "MailWebView.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>

int main(int argc, char **argv)
{
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
