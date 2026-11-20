#include "ClaudeDialog.h"

#include "Claude.h"
#include "ClaudeReports.h"
#include "Passwords.h"
#include "Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ClaudeDialog::ClaudeDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("claudeDialog");
    setWindowTitle(tr("Claude"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(6);

    auto *heading = new QLabel(tr("Claude"), this);
    heading->setObjectName("prefHeading");
    layout->addWidget(heading);
    auto *intro = new QLabel(tr("Extensions can ask Claude, Anthropic's AI, about your mail (Last Week with Claude). Nothing "
                                "is sent until you start it yourself, and you are told what first. You need an API key "
                                "from the <a href=\"https://platform.claude.com/\">Claude Console</a>; extensions never see it."),
                             this);
    intro->setObjectName("prefHint");
    intro->setWordWrap(true);
    intro->setOpenExternalLinks(true);
    QPalette linkPalette = intro->palette();
    linkPalette.setColor(QPalette::Link, currentTheme().accent);
    intro->setPalette(linkPalette);
    layout->addWidget(intro);
    layout->addSpacing(6);

    // API key.
    auto *keyRow = new QHBoxLayout;
    auto *key = new QLineEdit(this);
    key->setObjectName("claudeKey");
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(tr("Paste your API key (sk-ant-…)"));
    keyRow->addWidget(key, 1);
    auto *save = new QPushButton(tr("Save"), this);
    auto *test = new QPushButton(tr("Test"), this);
    auto *remove = new QPushButton(tr("Remove"), this);
    for (QPushButton *b : {save, test, remove}) {
        b->setObjectName("prefButton");
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoDefault(false);
        keyRow->addWidget(b);
    }
    layout->addLayout(keyRow);
    auto *keyStatus = new QLabel(this);
    keyStatus->setObjectName("prefHint");
    keyStatus->setWordWrap(true);
    layout->addWidget(keyStatus);

    auto showStored = [keyStatus, remove, test] {
        bool environment = false;
        const QString stored = Claude::storedKey(&environment);
        if (stored.isEmpty())
            keyStatus->setText(tr("No key saved yet."));
        else if (environment)
            keyStatus->setText(tr("The key ending in “…%1” comes from ANTHROPIC_API_KEY.").arg(stored.right(4)));
        else
            keyStatus->setText(tr("A key ending in “…%1” is kept with your passwords.").arg(stored.right(4)));
        remove->setEnabled(!stored.isEmpty() && !environment);
        test->setEnabled(!stored.isEmpty());
    };
    showStored();
    save->setEnabled(false);
    connect(key, &QLineEdit::textChanged, this, [key, test, save] {
        save->setEnabled(!key->text().trimmed().isEmpty());
        if (!key->text().trimmed().isEmpty())
            test->setEnabled(true);
    });
    connect(save, &QPushButton::clicked, this, [key, keyStatus, showStored] {
        QString error;
        if (!Claude::saveKey(key->text(), &error)) {
            keyStatus->setText(tr("Couldn't save the key: %1").arg(error));
            return;
        }
        key->clear();
        showStored();
    });
    connect(remove, &QPushButton::clicked, this, [showStored] {
        Claude::removeKey();
        showStored();
    });
    auto *client = new Claude::Client(this);
    connect(test, &QPushButton::clicked, this, [client, key, keyStatus, test] {
        const QString candidate = key->text().trimmed().isEmpty() ? Claude::storedKey() : key->text().trimmed();
        test->setEnabled(false);
        keyStatus->setText(tr("Checking the key…"));
        client->testKey(candidate, [keyStatus, test](bool ok, const QString &message) {
            test->setEnabled(true);
            keyStatus->setText((ok ? QStringLiteral("✓ ") : QStringLiteral("✗ ")) + message
                               + (ok ? tr(" (checked without spending anything)") : QString()));
        });
    });

    // Model.
    layout->addSpacing(10);
    auto *modelLabel = new QLabel(tr("Model"), this);
    modelLabel->setObjectName("prefSubheading");
    layout->addWidget(modelLabel);
    auto *modelBox = new QComboBox(this);
    modelBox->setObjectName("claudeModel");
    for (const Claude::ModelInfo &m : Claude::models()) {
        modelBox->addItem(tr("%1  ($%2 / $%3 per million tokens in / out)")
                              .arg(m.name, QString::number(m.inputPerMTok, 'g', 3), QString::number(m.outputPerMTok, 'g', 3)),
                          m.id);
    }
    modelBox->setCurrentIndex(modelBox->findData(Claude::model()));
    connect(modelBox, &QComboBox::currentIndexChanged, this, [modelBox] { Claude::setModel(modelBox->currentData().toString()); });
    layout->addWidget(modelBox);

    // Spending.
    layout->addSpacing(10);
    auto *spentRow = new QHBoxLayout;
    auto *spent = new QLabel(this);
    spent->setObjectName("prefHint");
    auto showSpent = [spent] { spent->setText(tr("Spent in Argonaut Emails so far: $%1").arg(Claude::spent(), 0, 'f', 2)); };
    showSpent();
    auto *reset = new QPushButton(tr("Reset"), this);
    reset->setObjectName("prefButton");
    reset->setAutoDefault(false);
    connect(reset, &QPushButton::clicked, this, [showSpent] {
        Claude::resetSpent();
        showSpent();
    });
    spentRow->addWidget(spent, 1);
    spentRow->addWidget(reset);
    layout->addLayout(spentRow);

    // Reports.
    layout->addSpacing(10);
    auto *reportsRow = new QHBoxLayout;
    auto *reports = new QLabel(tr("Every answer is kept, with what was sent, as a page in %1.")
                                   .arg(QDir::toNativeSeparators(ClaudeReports::folder())),
                               this);
    reports->setObjectName("prefHint");
    reports->setWordWrap(true);
    reports->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *open = new QPushButton(tr("Open Reports Folder"), this);
    open->setObjectName("prefButton");
    open->setAutoDefault(false);
    connect(open, &QPushButton::clicked, this, &ClaudeDialog::openReportsRequested);
    reportsRow->addWidget(reports, 1);
    reportsRow->addWidget(open);
    layout->addLayout(reportsRow);

    layout->addSpacing(12);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    // As wide as it should be, and as tall as its wrapped lines are then.
    resize(580, layout->totalHeightForWidth(580));
}
