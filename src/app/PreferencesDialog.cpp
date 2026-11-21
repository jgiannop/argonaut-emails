#include "PreferencesDialog.h"

#include "Account.h"
#include "Claude.h"
#include "Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr QSize PreviewSize(112, 72);
const int CheckChoices[] = {1, 2, 5, 10, 15, 30, 60};

// A tiny drawing of the mail window in the given theme: the folders at the
// left, the list and the message below it (or beside it).
void drawMockWindow(QPainter &p, const Theme &t, const QPainterPath &clip, bool beside)
{
    p.save();
    p.setClipPath(clip, Qt::IntersectClip);
    const QRectF r(0, 0, PreviewSize.width(), PreviewSize.height());
    p.fillRect(r, t.window);
    p.fillRect(QRectF(0, 0, 30, r.height()), t.sidebar);

    QColor bar = t.fg;
    bar.setAlpha(70);
    QColor faint = t.fg;
    faint.setAlpha(35);
    for (int i = 0; i < 5; ++i) {
        QPainterPath row;
        row.addRoundedRect(QRectF(5, 10 + i * 10, i == 0 ? 20 : 16, 4), 2, 2);
        p.fillPath(row, i == 0 ? t.accent : bar);
    }

    // The list: rows, the first chosen.
    const QRectF list = beside ? QRectF(34, 6, 30, r.height() - 12) : QRectF(34, 6, r.width() - 40, 24);
    const int rows = beside ? 5 : 3;
    const qreal step = list.height() / rows;
    for (int i = 0; i < rows; ++i) {
        const QRectF line(list.left(), list.top() + i * step, list.width(), step - 2);
        if (i == 0) {
            QPainterPath sel;
            sel.addRoundedRect(line, 2, 2);
            p.fillPath(sel, t.itemSelected);
        }
        QPainterPath text;
        text.addRoundedRect(QRectF(line.left() + 2, line.center().y() - 1.5, line.width() * (i % 2 ? 0.55 : 0.75), 3), 1.5, 1.5);
        p.fillPath(text, bar);
    }

    // The message: a header, lines of text.
    const QRectF message = beside ? QRectF(68, 6, r.width() - 74, r.height() - 12)
                                  : QRectF(34, 34, r.width() - 40, r.height() - 40);
    p.fillRect(QRectF(message.left(), message.top(), message.width(), 9), t.sidebar);
    QPainterPath subject;
    subject.addRoundedRect(QRectF(message.left() + 3, message.top() + 3, message.width() * 0.5, 3), 1.5, 1.5);
    p.fillPath(subject, t.fg);
    for (int i = 0; i < 4; ++i) {
        QPainterPath line;
        line.addRoundedRect(QRectF(message.left() + 3, message.top() + 14 + i * 6, message.width() * (i == 3 ? 0.45 : 0.85), 2.5), 1.25, 1.25);
        p.fillPath(line, faint);
    }
    p.restore();
}

QIcon previewIcon(const std::function<void(QPainter &, const QPainterPath &)> &draw)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(PreviewSize * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath frame;
    frame.addRoundedRect(QRectF(0.5, 0.5, PreviewSize.width() - 1, PreviewSize.height() - 1), 8, 8);
    draw(p, frame);
    p.setPen(QPen(QColor(0, 0, 0, 50), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(frame);
    p.end();
    return QIcon(pm);
}

QIcon themePreview(const QString &key)
{
    return previewIcon([key](QPainter &p, const QPainterPath &frame) {
        if (key == QLatin1String("system")) {
            // Split diagonally: light top-left, dark bottom-right.
            QPainterPath lightHalf;
            lightHalf.moveTo(0, 0);
            lightHalf.lineTo(PreviewSize.width(), 0);
            lightHalf.lineTo(0, PreviewSize.height());
            lightHalf.closeSubpath();
            QPainterPath darkHalf;
            darkHalf.addRect(QRectF(QPointF(0, 0), QSizeF(PreviewSize)));
            darkHalf = darkHalf.subtracted(lightHalf);
            drawMockWindow(p, Theme::forMode(ThemeMode::Light), frame.intersected(lightHalf), false);
            drawMockWindow(p, Theme::forMode(ThemeMode::Dark), frame.intersected(darkHalf), false);
        } else {
            drawMockWindow(p, themeForKey(key), frame, false);
        }
    });
}

QIcon panePreview(bool beside)
{
    return previewIcon([beside](QPainter &p, const QPainterPath &frame) { drawMockWindow(p, currentTheme(), frame, beside); });
}

QToolButton *previewCard(const QString &text, const QIcon &icon, QWidget *parent)
{
    auto *card = new QToolButton(parent);
    card->setObjectName("themeCard");
    card->setCheckable(true);
    card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    card->setText(text);
    card->setIcon(icon);
    card->setIconSize(PreviewSize);
    card->setCursor(Qt::PointingHandCursor);
    return card;
}

QLabel *heading(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName("prefHeading");
    return label;
}

QLabel *subheading(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName("prefSubheading");
    return label;
}

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName("prefHint");
    label->setWordWrap(true);
    return label;
}

QPushButton *button(const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName("prefButton");
    return b;
}

// A page: its widgets in a column that scrolls when the window is short.
QWidget *scrolled(QWidget *page, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName("prefScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    page->setObjectName("prefContent");
    scroll->setWidget(page);
    return scroll;
}

QVBoxLayout *pageLayout(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 22, 26, 24);
    layout->setSpacing(6);
    return layout;
}

Glyph sectionGlyph(int page)
{
    switch (page) {
    case PreferencesDialog::General: return Glyph::Menu;
    case PreferencesDialog::Appearance: return Glyph::Pictures;
    case PreferencesDialog::Accounts: return Glyph::Network;
    default: return Glyph::Cloud;
    }
}

} // namespace

int PreferencesDialog::checkMinutes()
{
    const int minutes = QSettings().value(QStringLiteral("mail/checkMinutes"), 5).toInt();
    return qBound(1, minutes, 60);
}

bool PreferencesDialog::push()
{
    return QSettings().value(QStringLiteral("mail/push"), true).toBool();
}

bool PreferencesDialog::fetchAhead()
{
    return QSettings().value(QStringLiteral("mail/fetchAhead"), true).toBool();
}

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("preferences");
    setWindowTitle(tr("Preferences"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName("prefSplitter");
    m_splitter->setChildrenCollapsible(false);
    outer->addWidget(m_splitter);

    m_sections = new QListWidget(m_splitter);
    m_sections->setObjectName("prefSections");
    m_sections->setIconSize(QSize(18, 18));
    m_sections->setMinimumWidth(150);
    m_sections->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QStringList names = {tr("General"), tr("Appearance"), tr("Accounts"), tr("Claude & Extensions")};
    for (const QString &name : names)
        new QListWidgetItem(name, m_sections);

    m_pages = new QStackedWidget(m_splitter);
    m_pages->setObjectName("prefPages");
    m_pages->addWidget(scrolled(buildGeneral(), m_pages));
    m_pages->addWidget(scrolled(buildAppearance(), m_pages));
    m_pages->addWidget(scrolled(buildAccounts(), m_pages));
    m_pages->addWidget(scrolled(buildClaude(), m_pages));
    m_pages->setMinimumWidth(380);
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    QSettings s;
    const QByteArray state = s.value(QStringLiteral("preferences/split")).toByteArray();
    if (state.isEmpty() || !m_splitter->restoreState(state))
        m_splitter->setSizes({190, 560});
    showPage(s.value(QStringLiteral("preferences/page"), 0).toInt());
    paintIcons();
    resize(780, 540);
}

void PreferencesDialog::paintIcons()
{
    const Theme t = currentTheme();
    for (int i = 0; i < m_sections->count(); ++i)
        m_sections->item(i)->setIcon(glyphIcon(sectionGlyph(i), t.fg));
    for (int id : {0, 1}) {
        if (QAbstractButton *b = m_pane->button(id))
            b->setIcon(panePreview(id == 1));
    }
    for (int i = 0; i < m_accounts->count(); ++i)
        m_accounts->item(i)->setIcon(glyphIcon(Glyph::Network, t.accent));
}

void PreferencesDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        paintIcons();
    QDialog::changeEvent(event);
}

void PreferencesDialog::showPage(int page)
{
    m_sections->setCurrentRow(qBound(0, page, PageCount - 1));
}

void PreferencesDialog::done(int result)
{
    QSettings s;
    s.setValue(QStringLiteral("preferences/split"), m_splitter->saveState());
    s.setValue(QStringLiteral("preferences/page"), m_sections->currentRow());
    QDialog::done(result);
}

QWidget *PreferencesDialog::buildGeneral()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    layout->addWidget(heading(tr("General"), page));
    layout->addWidget(hint(tr("How Argonaut Emails keeps your mail up to date."), page));
    layout->addSpacing(14);

    layout->addWidget(subheading(tr("System tray"), page));
    m_tray = new QCheckBox(tr("Keep running in the tray when the window is closed"), page);
    m_tray->setObjectName("prefCheck");
    m_tray->setChecked(QSettings().value(QStringLiteral("window/keepInTray"), true).toBool());
    connect(m_tray, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("window/keepInTray"), on);
        emit keepInTrayChanged(on);
    });
    layout->addWidget(m_tray);
    layout->addWidget(hint(tr("The tray icon shows how many messages are unread, and a click on it shows the newest. "
                              "File → Quit ends the program."),
                           page));
    layout->addSpacing(14);

    layout->addWidget(subheading(tr("New mail"), page));
    m_push = new QCheckBox(tr("Show new mail at once"), page);
    m_push->setObjectName("prefCheck");
    m_push->setChecked(push());
    connect(m_push, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("mail/push"), on);
        emit pushChanged(on);
    });
    layout->addWidget(m_push);
    layout->addWidget(hint(tr("The server tells at once when a message comes to the inbox (IMAP IDLE: Gmail and most "
                              "servers can). It keeps one more connection open."),
                           page));
    layout->addSpacing(8);
    auto *every = new QHBoxLayout;
    every->addWidget(new QLabel(tr("Look in every folder every"), page));
    m_checkEvery = new QComboBox(page);
    m_checkEvery->setObjectName("prefCombo");
    for (int minutes : CheckChoices)
        m_checkEvery->addItem(minutes == 1 ? tr("minute") : minutes == 60 ? tr("hour") : tr("%1 minutes").arg(minutes), minutes);
    const int index = m_checkEvery->findData(checkMinutes());
    m_checkEvery->setCurrentIndex(index < 0 ? m_checkEvery->findData(5) : index);
    connect(m_checkEvery, &QComboBox::currentIndexChanged, this, [this] {
        const int minutes = m_checkEvery->currentData().toInt();
        QSettings().setValue(QStringLiteral("mail/checkMinutes"), minutes);
        emit checkMinutesChanged(minutes);
    });
    every->addWidget(m_checkEvery);
    every->addStretch();
    layout->addLayout(every);
    layout->addWidget(hint(tr("And whenever you open a folder, or press F5. Without push, this is also when new "
                              "mail in the inbox shows."),
                           page));
    layout->addSpacing(8);

    m_fetchAhead = new QCheckBox(tr("Fetch the newest messages ahead, so they open at once"), page);
    m_fetchAhead->setObjectName("prefCheck");
    m_fetchAhead->setChecked(fetchAhead());
    connect(m_fetchAhead, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("mail/fetchAhead"), on);
        emit fetchAheadChanged(on);
    });
    layout->addWidget(m_fetchAhead);
    layout->addWidget(hint(tr("After each check, the text of the newest 30 messages not kept yet (up to 512 KB each, "
                              "not in the Trash or Spam). Their pictures still wait for Show Pictures."),
                           page));
    layout->addStretch();
    return page;
}

QWidget *PreferencesDialog::buildAppearance()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    layout->addWidget(heading(tr("Appearance"), page));
    layout->addWidget(hint(tr("Argonaut's themes: the same files, the same colours."), page));
    layout->addSpacing(12);

    layout->addWidget(subheading(tr("Theme"), page));
    auto *cards = new QGridLayout;
    cards->setSpacing(10);
    auto *themes = new QButtonGroup(this);
    const QString current = savedThemeKey();
    const QList<ThemeChoice> all = allThemes();
    constexpr int Columns = 3;
    for (int i = 0; i < all.size(); ++i) {
        const ThemeChoice theme = all.at(i);
        QToolButton *card = previewCard(theme.name, themePreview(theme.key), page);
        card->setChecked(theme.key == current);
        if (!theme.file.isEmpty())
            card->setToolTip(QDir::toNativeSeparators(theme.file));
        themes->addButton(card);
        cards->addWidget(card, i / Columns, i % Columns, Qt::AlignLeft);
        connect(card, &QToolButton::clicked, this, [key = theme.key] { setThemeKey(key); });
    }
    cards->setColumnStretch(Columns, 1);
    layout->addLayout(cards);
    layout->addSpacing(16);

    layout->addWidget(subheading(tr("Message pane"), page));
    auto *panes = new QHBoxLayout;
    panes->setSpacing(10);
    m_pane = new QButtonGroup(this);
    const bool beside = QSettings().value(QStringLiteral("window/messageBeside"), false).toBool();
    for (bool side : {false, true}) {
        QToolButton *card = previewCard(side ? tr("Beside the list") : tr("Below the list"), panePreview(side), page);
        card->setChecked(side == beside);
        m_pane->addButton(card, side ? 1 : 0);
        panes->addWidget(card);
    }
    panes->addStretch();
    connect(m_pane, &QButtonGroup::idClicked, this, [this](int id) { emit messageBesideChanged(id == 1); });
    layout->addLayout(panes);
    layout->addWidget(hint(tr("Beside the list, each message is two lines: who and when, then the subject."), page));
    layout->addStretch();
    return page;
}

QWidget *PreferencesDialog::buildAccounts()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    layout->addWidget(heading(tr("Accounts"), page));
    layout->addWidget(hint(tr("Your mail accounts. Passwords are kept in the desktop's keyring, never in the settings."), page));
    layout->addSpacing(12);

    m_accounts = new QListWidget(page);
    m_accounts->setObjectName("prefAccounts");
    m_accounts->setMinimumHeight(170);
    layout->addWidget(m_accounts, 1);
    m_noAccounts = hint(tr("No account yet: Add Account… to start."), page);
    layout->addWidget(m_noAccounts);

    auto *row = new QHBoxLayout;
    auto *add = button(tr("Add Account…"), page);
    auto *edit = button(tr("Settings…"), page);
    auto *remove = button(tr("Remove…"), page);
    row->addWidget(add);
    row->addWidget(edit);
    row->addStretch();
    row->addWidget(remove);
    layout->addLayout(row);
    connect(add, &QPushButton::clicked, this, &PreferencesDialog::addAccountRequested);
    connect(edit, &QPushButton::clicked, this, [this] {
        if (!chosenAccount().isEmpty())
            emit editAccountRequested(chosenAccount());
    });
    connect(m_accounts, &QListWidget::itemActivated, this, [this] { emit editAccountRequested(chosenAccount()); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (!chosenAccount().isEmpty())
            emit removeAccountRequested(chosenAccount());
    });
    auto enable = [this, edit, remove] {
        const bool chosen = !chosenAccount().isEmpty();
        edit->setEnabled(chosen);
        remove->setEnabled(chosen);
    };
    connect(m_accounts, &QListWidget::currentRowChanged, this, enable);
    reloadAccounts();
    enable();
    return page;
}

void PreferencesDialog::reloadAccounts()
{
    const QString chosen = chosenAccount();
    m_accounts->clear();
    for (const Account &a : Account::all()) {
        const QString line = a.name.isEmpty() ? a.email : QStringLiteral("%1\n%2").arg(a.name, a.email);
        auto *item = new QListWidgetItem(glyphIcon(Glyph::Network, currentTheme().accent), line, m_accounts);
        item->setData(Qt::UserRole, a.id);
        item->setToolTip(tr("Incoming %1, outgoing %2").arg(a.imapHost, a.smtpHost));
        if (a.id == chosen)
            m_accounts->setCurrentItem(item);
    }
    if (!m_accounts->currentItem() && m_accounts->count() > 0)
        m_accounts->setCurrentRow(0);
    m_noAccounts->setVisible(m_accounts->count() == 0);
}

QString PreferencesDialog::chosenAccount() const
{
    const QListWidgetItem *item = m_accounts ? m_accounts->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QWidget *PreferencesDialog::buildClaude()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    layout->addWidget(heading(tr("Claude & Extensions"), page));
    layout->addWidget(hint(tr("Extensions add commands; some ask Claude, Anthropic's AI, about your mail, with your "
                              "key and only when you choose. Every answer is kept in the Claude Reports folder."),
                           page));
    layout->addSpacing(12);

    layout->addWidget(subheading(tr("Claude"), page));
    m_claudeKey = hint(QString(), page);
    m_claudeModel = hint(QString(), page);
    m_claudeSpent = hint(QString(), page);
    layout->addWidget(m_claudeKey);
    layout->addWidget(m_claudeModel);
    layout->addWidget(m_claudeSpent);
    layout->addSpacing(4);
    auto *claudeRow = new QHBoxLayout;
    auto *claude = button(tr("Claude Settings…"), page);
    auto *reports = button(tr("Open Claude Reports"), page);
    claudeRow->addWidget(claude);
    claudeRow->addWidget(reports);
    claudeRow->addStretch();
    layout->addLayout(claudeRow);
    connect(claude, &QPushButton::clicked, this, &PreferencesDialog::claudeRequested);
    connect(reports, &QPushButton::clicked, this, &PreferencesDialog::reportsRequested);
    layout->addSpacing(16);

    layout->addWidget(subheading(tr("Extensions"), page));
    layout->addWidget(hint(tr("Switch them on or off, see what each may do (read your mail, ask Claude), or add your "
                              "own: an extension is a small program in any language."),
                           page));
    auto *extRow = new QHBoxLayout;
    auto *extensions = button(tr("Extensions…"), page);
    extRow->addWidget(extensions);
    extRow->addStretch();
    layout->addLayout(extRow);
    connect(extensions, &QPushButton::clicked, this, &PreferencesDialog::extensionsRequested);
    layout->addStretch();
    reloadClaude();
    return page;
}

void PreferencesDialog::reloadClaude()
{
    bool fromEnvironment = false;
    const bool hasKey = !Claude::storedKey(&fromEnvironment).isEmpty();
    m_claudeKey->setText(!hasKey          ? tr("No API key yet: Claude Settings… to add one.")
                         : fromEnvironment ? tr("API key: from ANTHROPIC_API_KEY.")
                                           : tr("API key: kept in the keyring."));
    m_claudeModel->setText(tr("Model: %1").arg(Claude::modelInfo(Claude::model()).name));
    m_claudeSpent->setText(tr("Spent in Argonaut Emails so far: $%1").arg(Claude::spent(), 0, 'f', 2));
}

void PreferencesDialog::setMessageBeside(bool beside)
{
    if (QAbstractButton *b = m_pane->button(beside ? 1 : 0))
        b->setChecked(true);
}

void PreferencesDialog::setKeepInTray(bool keep)
{
    const QSignalBlocker block(m_tray);
    m_tray->setChecked(keep);
}
