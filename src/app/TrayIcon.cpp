#include "TrayIcon.h"

#include "Theme.h"

#include <QCursor>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSystemTrayIcon>

#ifdef MAIL_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#endif

TrayIcon::TrayIcon(QMenu *menu, QObject *parent)
    : QObject(parent)
    , m_icon(new QSystemTrayIcon(this))
{
    m_icon->setContextMenu(menu);
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            // Where the icon is; an AppIndicator doesn't say: where the mouse is.
            QRect where = m_icon->geometry();
            if (!where.isValid() || where.isEmpty())
                where = QRect(QCursor::pos(), QSize(1, 1));
            emit clicked(where);
        } else if (reason == QSystemTrayIcon::MiddleClick) {
            emit openRequested();
        }
    });
    setUnread(0);
    if (isAvailable())
        m_icon->show();
}

bool TrayIcon::isAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

QIcon TrayIcon::badged(int unread)
{
    const QIcon base = applicationIcon();
    if (unread <= 0)
        return base;
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64}) {
        QPixmap pixmap = base.pixmap(size, size);
        pixmap.setDevicePixelRatio(1);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        const QString text = unread > 99 ? QStringLiteral("99+") : QString::number(unread);
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(qMax(7, size * (text.size() > 2 ? 30 : 38) / 100));
        p.setFont(font);
        const QFontMetrics fm(font);
        const int h = qMax(fm.height(), size * 46 / 100);
        const int w = qMax(h, fm.horizontalAdvance(text) + h / 2);
        const QRectF badge(size - w, 0, w, h);
        QPainterPath path;
        path.addRoundedRect(badge, h / 2.0, h / 2.0);
        p.fillPath(path, QColor(0xd9, 0x30, 0x25)); // Gmail's red
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, text);
        p.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

void TrayIcon::setUnread(int unread)
{
    if (unread == m_unread)
        return;
    m_unread = unread;
    m_icon->setIcon(badged(unread));
    m_icon->setToolTip(unread == 0 ? tr("Argonaut Emails") : tr("Argonaut Emails: %n unread", nullptr, unread));
    setDockCount(unread);
}

void TrayIcon::showMessage(const QString &title, const QString &text)
{
    if (m_icon->isVisible())
        m_icon->showMessage(title, text, applicationIcon(), 6000);
}

// The number on the dock's icon: Ubuntu's dock (and KDE's task manager)
// listen for com.canonical.Unity.LauncherEntry.Update, for the program's
// .desktop file.
void TrayIcon::setDockCount(int count)
{
#ifdef MAIL_DBUS
    QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/com/canonical/unity/launcherentry/argonaut_emails"),
                                                     QStringLiteral("com.canonical.Unity.LauncherEntry"), QStringLiteral("Update"));
    signal << QStringLiteral("application://argonaut-emails.desktop")
           << QVariantMap{{QStringLiteral("count"), qint64(count)}, {QStringLiteral("count-visible"), count > 0}};
    QDBusConnection::sessionBus().send(signal);
#else
    Q_UNUSED(count)
#endif
}
