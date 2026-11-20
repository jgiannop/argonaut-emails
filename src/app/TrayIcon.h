#pragma once

#include <QIcon>
#include <QObject>

class QMenu;
class QSystemTrayIcon;

// The program in the system tray, as Viber is (on Ubuntu, through its
// AppIndicator extension): the icon with the number of unread messages on
// it, as a phone's mail shows it, the same number on the dock's icon (the
// Unity launcher API, which Ubuntu's dock reads), a click for the popup of
// the newest messages, and a menu.
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QMenu *menu, QObject *parent = nullptr);

    // True when the desktop has a tray: then the icon is shown.
    bool isAvailable() const;
    QSystemTrayIcon *icon() const { return m_icon; }
    void setUnread(int unread);
    int unread() const { return m_unread; }
    void showMessage(const QString &title, const QString &text);

    // The icon with a red badge holding `unread` (99+); none at 0.
    static QIcon badged(int unread);

signals:
    void clicked(const QRect &where); // a left click: the popup
    void openRequested();             // a middle click: the window

private:
    void setDockCount(int count);

    QSystemTrayIcon *m_icon;
    int m_unread = -1;
};
