#pragma once

#include "MailStore.h"
#include "Theme.h"

#include <QFrame>

class QLabel;
class QListWidget;

// The tray icon's popup: the newest messages of every inbox (Gmail's
// Primary, as on a phone), ten at first and ten more each time the list is
// scrolled to its end. A click opens the message in the window.
class RecentPopup : public QFrame
{
    Q_OBJECT

public:
    static constexpr int Step = 10;
    static constexpr int Most = 300;

    explicit RecentPopup(MailStore *store, QWidget *parent = nullptr);

    // Shown near `anchor` (the tray icon), inside that screen.
    void popup(const QRect &anchor);
    void reload(); // the store changed: the same number, fresh
    void applyTheme(const Theme &theme);
    int shownCount() const;
    QListWidget *list() const { return m_list; }

    // The newest `count` messages of every inbox, newest first.
    static QList<MailStore::Summary> newest(MailStore *store, int count);
    // What a phone's badge says: unread in every inbox (in Gmail's Primary only).
    static int unread(MailStore *store);

signals:
    void messageChosen(const MailStore::Summary &summary);
    void openRequested();

private:
    void fill(int count);

    MailStore *m_store;
    QLabel *m_title = nullptr;
    QLabel *m_count = nullptr;
    QListWidget *m_list = nullptr;
    int m_wanted = Step;
    bool m_more = true; // there may be more to load
    Theme m_theme;
};
