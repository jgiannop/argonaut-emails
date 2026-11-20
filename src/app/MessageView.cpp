#include "MessageView.h"

#include "SafeHtml.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
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

MailBrowser::MailBrowser(QWidget *parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);
    setOpenExternalLinks(false);
}

QVariant MailBrowser::loadResource(int type, const QUrl &name)
{
    // Only the message's own pictures; nothing from the internet, no files.
    if (type == QTextDocument::ImageResource && m_message && name.scheme() == QLatin1String("cid")) {
        const Mime::Part *part = m_message->partById(name.path().toLatin1());
        if (part && part->type.startsWith("image/")) {
            QImage image = QImage::fromData(part->body);
            if (!image.isNull()) {
                const int width = qMax(100, viewport()->width() - 40);
                if (image.width() > width)
                    image = image.scaledToWidth(width, Qt::SmoothTransformation);
                return image;
            }
        }
    }
    return {};
}

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

    m_note = new QLabel(this);
    m_note->setObjectName(QStringLiteral("messageNote"));
    m_note->setTextFormat(Qt::PlainText);
    m_note->setContentsMargins(16, 6, 16, 6);
    m_note->setWordWrap(true);
    m_note->hide();
    layout->addWidget(m_note);

    m_body = new MailBrowser(this);
    m_body->setObjectName(QStringLiteral("messageBody"));
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->document()->setDocumentMargin(16);
    connect(m_body, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
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

    m_files = new QWidget(this);
    m_files->setObjectName(QStringLiteral("messageFiles"));
    m_filesLayout = new QHBoxLayout(m_files);
    m_filesLayout->setContentsMargins(12, 6, 12, 8);
    m_filesLayout->setSpacing(6);
    m_files->hide();
    layout->addWidget(m_files);

    showNote(tr("Choose a message to read it."));
}

void MessageView::showNote(const QString &text)
{
    m_hasMessage = false;
    m_message = Mime::Message();
    m_body->setMessage(nullptr);
    m_header->hide();
    m_files->hide();
    m_note->hide();
    m_body->setPlainText(QString());
    m_body->setHtml(QStringLiteral("<p align=\"center\" style=\"color:#888888\"><br><br>%1</p>").arg(text.toHtmlEscaped()));
}

void MessageView::showMessage(const QByteArray &raw)
{
    m_message = Mime::parse(raw);
    m_hasMessage = true;
    m_body->setMessage(&m_message);
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

    const QString html = m_message.html();
    int remote = 0;
    if (!html.isEmpty()) {
        const SafeHtml::Result clean = SafeHtml::clean(html);
        remote = clean.remotePictures;
        m_body->setHtml(clean.html);
    } else {
        m_body->setHtml(SafeHtml::fromPlainText(m_message.plainText()));
    }
    m_note->setText(remote == 1   ? tr("A picture from the internet wasn't loaded: it would tell the sender that you read this.")
                        : remote > 1 ? tr("%1 pictures from the internet weren't loaded: they would tell the sender that you read this.").arg(remote)
                                     : QString());
    m_note->setVisible(remote > 0);

    while (QLayoutItem *item = m_filesLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<Mime::Attachment> files = m_message.attachments();
    for (int i = 0; i < files.size(); ++i) {
        auto *button = new QPushButton(QStringLiteral("%1  (%2)").arg(files.at(i).fileName, sizeText(files.at(i).size)), m_files);
        button->setToolTip(tr("Save “%1”…").arg(files.at(i).fileName));
        button->setIcon(glyphIcon(Glyph::Documents, m_theme.dimFg));
        connect(button, &QPushButton::clicked, this, [this, i] { saveAttachment(i); });
        m_filesLayout->addWidget(button);
    }
    m_filesLayout->addStretch(1);
    m_files->setVisible(!files.isEmpty());
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
}

QString MessageView::shownText() const
{
    return m_body->toPlainText();
}
