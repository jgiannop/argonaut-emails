#include "MessageView.h"

#include "FlowLayout.h"

#include "MailWebView.h"
#include "SafeHtml.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

QString people(const QList<Mime::Address> &list)
{
    QStringList out;
    for (const Mime::Address &a : list)
        out.append(a.display());
    return out.join(QStringLiteral(", "));
}

} // namespace

MessageView::MessageView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("messageView"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("messageHeader"));
    auto *head = new QVBoxLayout(m_header);
    head->setContentsMargins(16, 12, 16, 10);
    head->setSpacing(3);
    m_subject = new QLabel(m_header);
    m_subject->setObjectName(QStringLiteral("messageSubject"));
    m_subject->setWordWrap(true);
    m_subject->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont big = m_subject->font();
    big.setPointSizeF(big.pointSizeF() * 1.3);
    big.setBold(true);
    m_subject->setFont(big);
    m_from = new QLabel(m_header);
    m_to = new QLabel(m_header);
    m_date = new QLabel(m_header);
    for (QLabel *l : {m_from, m_to, m_date}) {
        l->setTextFormat(Qt::PlainText);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
    }
    m_subject->setTextFormat(Qt::PlainText);
    head->addWidget(m_subject);
    head->addWidget(m_from);
    head->addWidget(m_to);
    head->addWidget(m_date);
    layout->addWidget(m_header);

    m_noteBar = new QWidget(this);
    m_noteBar->setObjectName(QStringLiteral("messageNoteBar"));
    auto *noteLayout = new QHBoxLayout(m_noteBar);
    noteLayout->setContentsMargins(16, 4, 12, 4);
    m_note = new QLabel(m_noteBar);
    m_note->setObjectName(QStringLiteral("messageNote"));
    m_note->setTextFormat(Qt::PlainText);
    m_note->setWordWrap(true);
    m_showPictures = new QPushButton(tr("Show Pictures"), m_noteBar);
    m_showPictures->setToolTip(tr("Load this message's pictures from the internet (the sender may learn that you read it)"));
    connect(m_showPictures, &QPushButton::clicked, this, [this] { showBody(true); });
    noteLayout->addWidget(m_note, 1);
    noteLayout->addWidget(m_showPictures);
    m_noteBar->hide();
    layout->addWidget(m_noteBar);

    m_body = new MailWebView(this);
    m_body->setObjectName(QStringLiteral("messageBody"));
    connect(m_body, &MailWebView::linkClicked, this, [this](const QUrl &url) {
        // A link opens only after the user saw where it really goes.
        const QString scheme = url.scheme().toLower();
        if (scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("mailto"))
            return;
        if (QMessageBox::question(this, tr("Open Link"), tr("Open this address in your browser?\n\n%1").arg(url.toDisplayString()),
                                  QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel)
            == QMessageBox::Open)
            QDesktopServices::openUrl(url);
    });
    layout->addWidget(m_body, 1);

    // The attachments wrap onto more lines rather than widen the window (a
    // message with twenty of them would push the folders and the list away).
    m_files = new QScrollArea(this);
    m_files->setObjectName(QStringLiteral("messageFiles"));
    m_files->setFrameShape(QFrame::NoFrame);
    m_files->setWidgetResizable(true);
    m_files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_files->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto *filesBox = new QWidget(m_files);
    filesBox->setObjectName(QStringLiteral("messageFilesBox"));
    m_filesLayout = new FlowLayout(filesBox, 6);
    m_filesLayout->setContentsMargins(12, 6, 12, 8);
    m_files->setWidget(filesBox);
    m_files->hide();
    layout->addWidget(m_files);

    showNote(tr("Choose a message to read it."));
}

void MessageView::showNote(const QString &text)
{
    m_hasMessage = false;
    m_hasResult = false;
    m_noteText = text;
    m_body->showNote(text); // first: the page lets go of the message
    m_message = Mime::Message();
    m_header->hide();
    m_files->hide();
    m_noteBar->hide();
}

void MessageView::showResult(const QString &title, const QString &html, const QString &note)
{
    m_hasMessage = false;
    m_hasResult = true;
    m_noteText.clear();
    m_resultHtml = html;
    m_body->showHtml(html, nullptr, false, true); // first: the page lets go of the message
    m_message = Mime::Message();
    m_subject->setText(title);
    m_from->setText(note);
    m_from->setVisible(!note.isEmpty());
    m_to->hide();
    m_date->hide();
    m_header->show();
    m_files->hide();
    m_noteBar->hide();
}

void MessageView::showMessage(const QByteArray &raw)
{
    m_message = Mime::parse(raw);
    m_hasMessage = true;
    m_hasResult = false;
    m_from->show();
    m_subject->setText(m_message.subject().isEmpty() ? tr("(no subject)") : m_message.subject());
    m_from->setText(tr("From: %1").arg(people(m_message.from())));
    QString to = people(m_message.to());
    const QString cc = people(m_message.cc());
    if (!cc.isEmpty())
        to += tr("   Cc: %1").arg(cc);
    m_to->setText(tr("To: %1").arg(to));
    m_to->setVisible(!to.isEmpty());
    const QDateTime date = m_message.date();
    m_date->setText(date.isValid() ? QLocale().toString(date.toLocalTime(), QLocale::LongFormat) : QString());
    m_date->setVisible(date.isValid());
    m_header->show();
    showBody(false);

    while (QLayoutItem *item = m_filesLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<Mime::Attachment> files = m_message.attachments();
    for (int i = 0; i < files.size(); ++i) {
        // A long name is shortened in the middle (its end says what it is); the tooltip has all of it.
        const QString name = fontMetrics().elidedText(files.at(i).fileName, Qt::ElideMiddle, 260);
        auto *button = new QPushButton(QStringLiteral("%1  (%2)").arg(name, sizeText(files.at(i).size)), m_files->widget());
        button->setToolTip(tr("Save “%1”…").arg(files.at(i).fileName));
        button->setIcon(glyphIcon(Glyph::Documents, m_theme.dimFg));
        connect(button, &QPushButton::clicked, this, [this, i] { saveAttachment(i); });
        m_filesLayout->addWidget(button);
    }
    m_files->setVisible(!files.isEmpty());
    m_files->verticalScrollBar()->setValue(0);
    fitFiles();
}

void MessageView::fitFiles()
{
    if (!m_files || m_files->isHidden() || m_filesLayout->count() == 0)
        return;
    const int width = qMax(1, m_files->viewport()->width());
    const int needed = m_filesLayout->heightForWidth(width);
    const int line = m_filesLayout->itemAt(0)->sizeHint().height();
    const QMargins margins = m_filesLayout->contentsMargins();
    const int threeLines = margins.top() + 3 * line + 3 * 6; // up to the fourth line, which the scroll bar reaches
    m_files->setFixedHeight(qMin(needed, threeLines));
}

void MessageView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitFiles();
}

void MessageView::showBody(bool remotePictures)
{
    if (!m_hasMessage)
        return;
    const QString html = m_message.html();
    int remote = 0;
    if (!html.isEmpty()) {
        const SafeHtml::Result clean = SafeHtml::clean(html);
        remote = clean.remotePictures;
        m_body->showHtml(clean.html, &m_message, remotePictures, false);
    } else {
        m_body->showHtml(SafeHtml::fromPlainText(m_message.plainText()), &m_message, false, true);
    }
    m_note->setText(remote == 1   ? tr("A picture from the internet wasn't loaded: it would tell the sender that you read this.")
                        : remote > 1 ? tr("%1 pictures from the internet weren't loaded: they would tell the sender that you read this.").arg(remote)
                                     : QString());
    m_noteBar->setVisible(remote > 0 && !remotePictures);
}

void MessageView::saveAttachment(int index)
{
    const QList<Mime::Attachment> files = m_message.attachments();
    if (index < 0 || index >= files.size() || !files.at(index).part)
        return;
    const Mime::Attachment &file = files.at(index);
    QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (folder.isEmpty())
        folder = QDir::homePath();
    // The name was cleaned by Mime (no folders); the dialog never overwrites without asking.
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Attachment"), folder + QLatin1Char('/') + file.fileName);
    if (path.isEmpty())
        return;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(file.part->body) != file.part->body.size() || !out.commit())
        QMessageBox::warning(this, tr("Save Attachment"), tr("“%1” couldn't be saved: %2").arg(path, out.errorString()));
}

void MessageView::applyTheme(const Theme &theme)
{
    m_theme = theme;
    m_body->setColors(theme.window, theme.fg);
    // What is shown, again in the new colours (a designed message stays on white, its pictures as they were).
    if (m_hasMessage)
        showBody(m_body->remotePicturesAllowed());
    else if (m_hasResult)
        m_body->showHtml(m_resultHtml, nullptr, false, true);
    else if (!m_noteText.isEmpty())
        m_body->showNote(m_noteText);
}

QString MessageView::shownText() const
{
    return m_body->shownText();
}

bool MessageView::picturesShown() const
{
    return m_hasMessage && m_body->remotePicturesAllowed();
}
