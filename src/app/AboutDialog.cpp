#include "AboutDialog.h"

#include "Account.h"
#include "ClaudeReports.h"
#include "Extensions.h"
#include "MailStore.h"
#include "Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDir>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QSysInfo>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>
#include <qtwebenginecoreversion.h>

namespace {

struct Detail
{
    QString label;
    QString value;
};

QList<Detail> details()
{
    QList<Detail> list;
    list.append({QObject::tr("Argonaut Emails"), QStringLiteral(MAIL_VERSION)});
    list.append({QObject::tr("Qt"), QObject::tr("%1 (built with %2)").arg(QString::fromLatin1(qVersion()),
                                                                        QStringLiteral(QT_VERSION_STR))});
    list.append({QObject::tr("Qt WebEngine"), QStringLiteral(QTWEBENGINECORE_VERSION_STR)});
    list.append({QObject::tr("System"), QSysInfo::prettyProductName()});
    list.append({QObject::tr("Kernel"), QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion()});
    list.append({QObject::tr("Architecture"), QSysInfo::currentCpuArchitecture()});
    QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    desktop.replace(QLatin1Char(':'), QStringLiteral(", "));
    const QString platform = QGuiApplication::platformName();
    list.append({QObject::tr("Desktop"), desktop.isEmpty() ? platform : QStringLiteral("%1 (%2)").arg(desktop, platform)});
    list.append({QObject::tr("System tray"),
                 QSystemTrayIcon::isSystemTrayAvailable() ? QObject::tr("Yes") : QObject::tr("Not on this desktop")});
#if defined(MAIL_DBUS)
    list.append({QObject::tr("Passwords"), QObject::tr("The desktop's keyring (else a file only you can read)")});
#else
    list.append({QObject::tr("Passwords"), QObject::tr("A file only you can read (no D-Bus in this build)")});
#endif
    list.append({QObject::tr("Accounts"), QString::number(Account::all().size())});
    list.append({QObject::tr("Mail kept in"), QDir::toNativeSeparators(MailStore::defaultFile())});
    list.append({QObject::tr("Claude reports"), QDir::toNativeSeparators(ClaudeReports::folder())});
    list.append({QObject::tr("Extensions"), QDir::toNativeSeparators(Extensions::userFolder())});
    list.append({QObject::tr("Settings"), QDir::toNativeSeparators(QSettings().fileName())});
    return list;
}

QLabel *paragraph(const QString &text, QWidget *parent, const char *objectName = "aboutText")
{
    // The style sheet hides the palette's link colour: the accent, written in.
    QString html = text;
    html.replace(QStringLiteral("<a href"),
                 QStringLiteral("<a style=\"color: %1; text-decoration: none;\" href").arg(currentTheme().accent.name()));
    auto *label = new QLabel(html, parent);
    label->setObjectName(objectName);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    return label;
}

// A rounded card with a bold heading and a paragraph.
QWidget *card(const QString &heading, const QString &html, QWidget *parent)
{
    auto *box = new QWidget(parent);
    box->setObjectName("aboutCard");
    box->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(14, 10, 14, 12);
    layout->setSpacing(3);
    auto *title = new QLabel(heading, box);
    title->setObjectName("aboutCardHeading");
    layout->addWidget(title);
    layout->addWidget(paragraph(html, box));
    return box;
}

// A small envelope, the banner's decoration.
void drawEnvelope(QPainter &p, QPointF centre, qreal width, qreal angle, const QColor &colour)
{
    p.save();
    p.translate(centre);
    p.rotate(angle);
    const qreal h = width * 0.66;
    const QRectF r(-width / 2, -h / 2, width, h);
    QPainterPath body;
    body.addRoundedRect(r, width * 0.1, width * 0.1);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawPath(body);
    QColor fold = colour;
    fold.setAlphaF(qMin(1.0, colour.alphaF() * 1.8));
    p.setPen(QPen(fold, qMax(1.0, width / 18), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath flap;
    flap.moveTo(r.left() + width * 0.08, r.top() + h * 0.14);
    flap.lineTo(0, h * 0.08);
    flap.lineTo(r.right() - width * 0.08, r.top() + h * 0.14);
    p.drawPath(flap);
    p.restore();
}

// A coloured round badge with a glyph, for the cards of the About page.
QPixmap badge(Glyph glyph, const QColor &colour, qreal dpr, bool lock = false)
{
    constexpr int Size = 30;
    QPixmap pm(QSize(Size, Size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, Size, Size);
    g.setColorAt(0, colour.lighter(125));
    g.setColorAt(1, colour.darker(125));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(QRectF(0, 0, Size, Size));
    if (lock) {
        // A padlock: there is no glyph for it.
        p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(10.5, 7, 9, 10), 0, 180 * 16);
        p.drawLine(QPointF(10.5, 12), QPointF(10.5, 14));
        p.drawLine(QPointF(19.5, 12), QPointF(19.5, 14));
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(8.5, 13.5, 13, 9.5), 2, 2);
    } else {
        glyphIcon(glyph, Qt::white).paint(&p, QRect(7, 7, Size - 14, Size - 14));
    }
    return pm;
}

// The banner: the accent as a sky at dusk, two waves, envelopes sailing,
// a glow behind the icon. Its labels are children laid out on top.
class Hero : public QWidget
{
public:
    explicit Hero(QWidget *parent) : QWidget(parent) { setObjectName("aboutHero"); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme t = currentTheme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect();
        QPainterPath clip;
        clip.addRoundedRect(r, 16, 16);
        p.setClipPath(clip);

        QLinearGradient sky(r.topLeft(), r.bottomRight());
        sky.setColorAt(0, t.accent.lighter(135));
        sky.setColorAt(0.55, t.accent);
        sky.setColorAt(1, t.accent.darker(170));
        p.fillRect(r, sky);

        // A glow behind the icon (its centre is where the layout puts it).
        const QPointF glowAt(r.center().x(), 72);
        QRadialGradient glow(glowAt, 110);
        glow.setColorAt(0, QColor(255, 255, 255, 95));
        glow.setColorAt(0.45, QColor(255, 255, 255, 30));
        glow.setColorAt(1, QColor(255, 255, 255, 0));
        p.fillRect(r, glow);

        // Envelopes sailing past, bigger ones nearer.
        struct Flying { qreal x, y, w, angle; int alpha; };
        const Flying flying[] = {
            {0.09, 0.22, 26, -14, 70}, {0.20, 0.62, 16, 9, 55},  {0.13, 0.86, 12, -4, 40},
            {0.86, 0.18, 22, 12, 65},  {0.93, 0.52, 14, -10, 50}, {0.78, 0.78, 18, 6, 55},
            {0.30, 0.12, 10, 18, 40},  {0.70, 0.40, 11, -20, 40},
        };
        for (const Flying &f : flying)
            drawEnvelope(p, QPointF(r.width() * f.x, r.height() * f.y), f.w, f.angle, QColor(255, 255, 255, f.alpha));

        // Two waves along the bottom, Argonaut's sea.
        auto wave = [&](qreal base, qreal height, qreal phase, int alpha) {
            QPainterPath path;
            path.moveTo(r.left(), r.bottom());
            for (qreal x = r.left(); x <= r.right() + 1; x += 4)
                path.lineTo(x, base + height * qSin(x / r.width() * 2 * M_PI * 1.5 + phase));
            path.lineTo(r.right(), r.bottom());
            path.closeSubpath();
            p.fillPath(path, QColor(255, 255, 255, alpha));
        };
        wave(r.bottom() - 26, 6, 0.0, 28);
        wave(r.bottom() - 16, 5, 2.1, 40);
    }
};

} // namespace

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("aboutDialog");
    setWindowTitle(tr("About Argonaut Emails"));
    setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(0);

    // The banner: icon, name, version, tagline.
    auto *hero = new Hero(this);
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(20, 22, 20, 34);
    heroLayout->setSpacing(0);
    auto *icon = new QLabel(hero);
    icon->setPixmap(applicationIcon().pixmap(QSize(104, 104), devicePixelRatioF()));
    icon->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(icon);
    heroLayout->addSpacing(8);
    auto *name = new QLabel(QStringLiteral("Argonaut Emails"), hero);
    name->setObjectName("aboutName");
    name->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(name);
    heroLayout->addSpacing(8);
    auto *versionRow = new QHBoxLayout;
    versionRow->addStretch();
    auto *version = new QLabel(tr("Version %1").arg(QStringLiteral(MAIL_VERSION)), hero);
    version->setObjectName("aboutVersion");
    versionRow->addWidget(version);
    versionRow->addStretch();
    heroLayout->addLayout(versionRow);
    heroLayout->addSpacing(10);
    auto *tagline = new QLabel(tr("Your mail, quick and private, the Argonaut way"), hero);
    tagline->setObjectName("aboutTagline");
    tagline->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(tagline);
    layout->addWidget(hero);
    layout->addSpacing(16);

    // Page switcher: About · System · Credits.
    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(4);
    tabs->addStretch();
    auto *group = new QButtonGroup(this);
    const QStringList names = {tr("About"), tr("System"), tr("Credits")};
    for (int i = 0; i < names.size(); ++i) {
        auto *tab = new QToolButton(this);
        tab->setObjectName("aboutTab");
        tab->setText(names[i]);
        tab->setCheckable(true);
        tab->setChecked(i == 0);
        tab->setCursor(Qt::PointingHandCursor);
        group->addButton(tab, i);
        tabs->addWidget(tab);
    }
    tabs->addStretch();
    layout->addLayout(tabs);
    layout->addSpacing(14);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildAboutPage());
    m_pages->addWidget(buildSystemPage());
    m_pages->addWidget(buildCreditsPage());
    layout->addWidget(m_pages, 1);
    connect(group, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);

    layout->addSpacing(16);
    auto *buttons = new QHBoxLayout;
    auto *copy = new QPushButton(tr("Copy Details"), this);
    copy->setObjectName("prefButton");
    copy->setToolTip(tr("Copy the version and system details, for a bug report"));
    connect(copy, &QPushButton::clicked, this, [copy] {
        QApplication::clipboard()->setText(systemDetails());
        copy->setText(tr("Copied"));
        QTimer::singleShot(1500, copy, [copy] { copy->setText(tr("Copy Details")); });
    });
    buttons->addWidget(copy);
    buttons->addStretch();
    auto *close = new QPushButton(tr("Close"), this);
    close->setObjectName("aboutClose");
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    setFixedWidth(540);
}

QString AboutDialog::systemDetails()
{
    QStringList lines;
    for (const Detail &d : details())
        lines << d.label + QStringLiteral(": ") + d.value;
    return lines.join(QLatin1Char('\n'));
}

QWidget *AboutDialog::buildAboutPage()
{
    auto *page = new QWidget(m_pages);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(paragraph(
        tr("Argonaut Emails is a mail program for IMAP and SMTP accounts, Gmail's included: three panes, "
           "messages that open at once, and the look of Argonaut, the file manager."),
        page));

    const Theme t = currentTheme();
    struct Feature { Glyph glyph; QColor colour; QString heading; QString text; bool lock = false; };
    const QList<Feature> features = {
        {Glyph::Recent, t.accent, tr("Quick"), tr("Messages open at once, their text fetched ahead.")},
        {Glyph::Close, QColor(0x2e, 0xc2, 0x7e), tr("Private"), tr("No scripts, no trackers: pictures only when you ask."), true},
        {Glyph::Star, QColor(0xf5, 0xc2, 0x11), tr("Gmail's tabs"), tr("Primary, Promotions, Social, Updates and Forums.")},
        {Glyph::Cloud, QColor(0x91, 0x41, 0xac), tr("Claude, if you like"), tr("Extensions, and a weekly report of your mail.")},
    };
    auto *grid = new QGridLayout;
    grid->setSpacing(8);
    for (int i = 0; i < features.size(); ++i) {
        auto *box = new QWidget(page);
        box->setObjectName("aboutCard");
        box->setAttribute(Qt::WA_StyledBackground);
        auto *row = new QHBoxLayout(box);
        row->setContentsMargins(12, 10, 12, 10);
        row->setSpacing(10);
        auto *icon = new QLabel(box);
        icon->setPixmap(badge(features[i].glyph, features[i].colour, devicePixelRatioF(), features[i].lock));
        icon->setAlignment(Qt::AlignTop);
        row->addWidget(icon);
        auto *texts = new QVBoxLayout;
        texts->setSpacing(2);
        auto *heading = new QLabel(features[i].heading, box);
        heading->setObjectName("aboutCardHeading");
        texts->addWidget(heading);
        texts->addWidget(paragraph(features[i].text, box, "aboutCardText"));
        row->addLayout(texts, 1);
        grid->addWidget(box, i / 2, i % 2);
    }
    layout->addLayout(grid);
    layout->addWidget(paragraph(tr("Named after the Argonauts, who sailed on the <i>Argo</i> into the unknown, "
                                   "and brought back news from far away."),
                                page, "aboutDim"));
    layout->addWidget(paragraph(tr("<a href=\"https://github.com/jgiannop/argonaut-emails\">Website</a> · "
                                   "<a href=\"https://github.com/jgiannop/argonaut-emails/issues\">Report a problem</a> · "
                                   "<a href=\"https://github.com/jgiannop/argonaut-emails/releases\">Releases</a>"),
                                page));
    layout->addStretch();
    return page;
}

QWidget *AboutDialog::buildSystemPage()
{
    auto *page = new QWidget(m_pages);
    page->setObjectName("aboutCard");
    page->setAttribute(Qt::WA_StyledBackground);
    auto *grid = new QGridLayout(page);
    grid->setContentsMargins(14, 12, 14, 12);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(5);
    int row = 0;
    for (const Detail &d : details()) {
        auto *label = new QLabel(d.label, page);
        label->setObjectName("aboutLabel");
        label->setAlignment(Qt::AlignRight | Qt::AlignTop);
        // Long paths are shortened in the middle; the tooltip and Copy Details have them whole.
        auto *value = new QLabel(page);
        const QString shown = value->fontMetrics().elidedText(d.value, Qt::ElideMiddle, 320);
        value->setText(shown);
        if (shown != d.value)
            value->setToolTip(d.value);
        value->setObjectName("aboutValue");
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(label, row, 0);
        grid->addWidget(value, row, 1);
        ++row;
    }
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(row, 1);
    return page;
}

QWidget *AboutDialog::buildCreditsPage()
{
    auto *page = new QWidget(m_pages);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(card(tr("Built with"),
                           tr("<a href=\"https://www.qt.io\">Qt</a> %1 for the interface, "
                              "Qt WebEngine (Chromium) to show messages, locked, and SQLite to keep them.")
                               .arg(QString::fromLatin1(qVersion())),
                           page));
    layout->addWidget(card(tr("Family"),
                           tr("A sibling of <a href=\"https://github.com/jgiannop/argonaut\">Argonaut</a>, the file "
                              "manager: its themes, its command palette, its extensions and its icon's boat."),
                           page));
    layout->addWidget(card(tr("License"),
                           tr("Copyright © 2026 johngiannop. Argonaut Emails is free software: you can share and "
                              "change it under the <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU General "
                              "Public License, version 3 or later</a>. It comes with no warranty."),
                           page));
    layout->addStretch();
    return page;
}
