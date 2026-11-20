#include "ExtensionsDialog.h"

#include "Extensions.h"

#include <QCheckBox>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

using Extensions::Host;

namespace {

QString stateText(Host::State state)
{
    switch (state) {
    case Host::State::Running:
        return ExtensionsDialog::tr("Running");
    case Host::State::Waiting:
        return ExtensionsDialog::tr("On");
    case Host::State::Stopped:
        return ExtensionsDialog::tr("Stopped: it kept failing");
    case Host::State::Unusable:
        return ExtensionsDialog::tr("Can't be used");
    case Host::State::Off:
        break;
    }
    return ExtensionsDialog::tr("Off");
}

} // namespace

ExtensionsDialog::ExtensionsDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("extensionsDialog");
    setWindowTitle(tr("Extensions"));
    resize(560, 560);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 20, 24, 18);
    outer->setSpacing(6);
    auto *heading = new QLabel(tr("Extensions"), this);
    heading->setObjectName("prefHeading");
    auto *hint = new QLabel(tr("Small programs that add commands to the menus and to the command palette "
                               "(Ctrl+Shift+P). An extension can do anything you can, so switch on only those you trust."),
                            this);
    hint->setObjectName("prefHint");
    hint->setWordWrap(true);
    outer->addWidget(heading);
    outer->addWidget(hint);
    outer->addSpacing(8);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("prefScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    content->setObjectName("prefContent");
    scroll->setWidget(content);
    m_list = new QVBoxLayout(content);
    m_list->setContentsMargins(0, 0, 0, 0);
    m_list->setSpacing(10);
    outer->addWidget(scroll, 1);
    outer->addSpacing(8);

    // Two kinds, in two places: say so, or the user's (empty) folder looks wrong.
    auto *where = new QLabel(tr("Extensions that come with Argonaut Emails are part of the program. Ones you add yourself "
                                "go in your extensions folder (%1), which is empty until you add one.")
                                 .arg(QDir::toNativeSeparators(Extensions::userFolder())),
                             this);
    where->setObjectName("prefHint");
    where->setWordWrap(true);
    where->setTextFormat(Qt::PlainText);
    outer->addWidget(where);
    outer->addSpacing(6);

    auto *buttons = new QHBoxLayout;
    auto *folder = new QPushButton(tr("Open My Extensions Folder"), this);
    auto *look = new QPushButton(tr("Look Again"), this);
    auto *guide = new QPushButton(tr("About Extensions"), this);
    for (QPushButton *b : {folder, look, guide}) {
        b->setObjectName("prefButton");
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoDefault(false);
    }
    folder->setToolTip(tr("Where extensions you add yourself go. The ones that come with the program aren't there."));
    look->setToolTip(tr("Read the extension folders again"));
    buttons->addWidget(folder);
    buttons->addWidget(look);
    buttons->addStretch();
    buttons->addWidget(guide);
    outer->addLayout(buttons);

    Host *host = Host::instance();
    connect(folder, &QPushButton::clicked, this, [this] { emit openFolderRequested(Extensions::userFolder()); });
    connect(look, &QPushButton::clicked, host, &Host::rescan);
    connect(guide, &QPushButton::clicked, this, &ExtensionsDialog::guideRequested);
    // Queued: a switch that was just clicked is inside the list being rebuilt.
    connect(host, &Host::extensionsChanged, this, &ExtensionsDialog::rebuild, Qt::QueuedConnection);
    auto *tick = new QTimer(this); // running / stopped changes by itself
    tick->setInterval(1000);
    connect(tick, &QTimer::timeout, this, &ExtensionsDialog::updateStates);
    tick->start();

    host->rescan(); // one put in the folder since the last look
    rebuild();
}

void ExtensionsDialog::rebuild()
{
    while (QLayoutItem *item = m_list->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    Host *host = Host::instance();
    const QList<Extensions::Manifest> found = host->extensions();
    for (const Extensions::Manifest &m : found) {
        auto *card = new QFrame(this);
        card->setObjectName("extCard");
        auto *box = new QVBoxLayout(card);
        box->setContentsMargins(14, 12, 14, 12);
        box->setSpacing(4);

        auto *top = new QHBoxLayout;
        auto *name = new QLabel(m.name, card);
        name->setObjectName("extName");
        name->setTextFormat(Qt::PlainText);
        auto *version = new QLabel(m.version, card);
        version->setObjectName("prefHint");
        version->setTextFormat(Qt::PlainText);
        auto *state = new QLabel(card);
        state->setObjectName("extState");
        state->setProperty("extensionId", m.id);
        auto *toggle = new QCheckBox(card);
        toggle->setObjectName("prefCheck");
        toggle->setAccessibleName(tr("Switch %1 on").arg(m.name));
        toggle->setChecked(m.isUsable() && host->isEnabled(m.id));
        toggle->setEnabled(m.isUsable());
        toggle->setCursor(Qt::PointingHandCursor);
        top->addWidget(name);
        top->addWidget(version);
        top->addStretch();
        top->addWidget(state);
        top->addSpacing(8);
        top->addWidget(toggle);
        box->addLayout(top);

        QStringList lines;
        if (!m.isUsable())
            lines << m.problem;
        if (!m.description.isEmpty())
            lines << m.description;
        // It reads your mail, through the program, when you choose one of its commands.
        if (m.readsMail)
            lines << tr("Reads your messages when you choose one of its commands");
        // It asks Claude through the program: with your key, at your cost.
        if (m.usesClaude) {
            const double spent = Extensions::Host::claudeSpent(m.id);
            lines << (spent > 0 ? tr("Uses Claude, with your key and at your cost: $%1 so far").arg(QString::number(spent, 'f', spent < 1 ? 3 : 2))
                                : tr("Uses Claude, with your key and at your cost"));
        }
        QString source = m.builtIn ? tr("Comes with Argonaut Emails")
                                   : tr("Added by you: %1").arg(QDir::toNativeSeparators(m.folder));
        if (!m.author.isEmpty() && !m.builtIn)
            source = tr("By %1. ").arg(m.author) + source;
        lines << source;
        auto *text = new QLabel(lines.join(QLatin1Char('\n')), card);
        text->setObjectName("prefHint");
        text->setWordWrap(true);
        text->setTextFormat(Qt::PlainText);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        box->addWidget(text);

        const QString id = m.id;
        connect(toggle, &QCheckBox::toggled, this, [id](bool on) { Host::instance()->setEnabled(id, on); });
        m_list->addWidget(card);
    }
    if (found.isEmpty()) {
        auto *none = new QLabel(tr("No extensions found."), this);
        none->setObjectName("prefHint");
        m_list->addWidget(none);
    }
    m_list->addStretch();
    updateStates();
}

void ExtensionsDialog::updateStates()
{
    Host *host = Host::instance();
    const QList<QLabel *> labels = findChildren<QLabel *>(QStringLiteral("extState"));
    for (QLabel *label : labels) {
        const Host::State state = host->state(label->property("extensionId").toString());
        label->setText(stateText(state));
        const char *look = state == Host::State::Running ? "good"
                           : state == Host::State::Stopped || state == Host::State::Unusable ? "bad" : "plain";
        if (label->property("look").toString() != QLatin1String(look)) {
            label->setProperty("look", look);
            label->setStyleSheet(QStringLiteral("/* */")); // re-read the property
        }
    }
}
