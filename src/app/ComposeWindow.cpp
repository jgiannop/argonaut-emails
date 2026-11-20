#include "ComposeWindow.h"

#include "SmtpClient.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr qint64 MaxAttachments = 25 * 1000 * 1000; // what most servers take

QList<Mime::Address> addressesIn(const QString &text)
{
    return Mime::parseAddresses(text);
}

} // namespace

ComposeWindow::ComposeWindow(const QString &accountId, QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("New Message"));
    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    m_from = new QComboBox(this);
    for (const Account &a : Account::all()) {
        m_from->addItem(a.name.isEmpty() ? a.email : QStringLiteral("%1 <%2>").arg(a.name, a.email), a.id);
        if (a.id == accountId)
            m_from->setCurrentIndex(m_from->count() - 1);
    }
    m_to = new QLineEdit(this);
    m_cc = new QLineEdit(this);
    m_bcc = new QLineEdit(this);
    m_subject = new QLineEdit(this);
    m_to->setPlaceholderText(tr("Addresses, separated by commas"));
    form->addRow(tr("From"), m_from);
    form->addRow(tr("To"), m_to);
    form->addRow(tr("Cc"), m_cc);
    form->addRow(tr("Bcc"), m_bcc);
    form->addRow(tr("Subject"), m_subject);
    layout->addLayout(form);
    connect(m_subject, &QLineEdit::textChanged, this, [this](const QString &s) {
        setWindowTitle(s.trimmed().isEmpty() ? tr("New Message") : s);
    });

    m_text = new QPlainTextEdit(this);
    m_text->setTabChangesFocus(true);
    layout->addWidget(m_text, 1);

    m_files = new QListWidget(this);
    m_files->setMaximumHeight(80);
    m_files->hide();
    layout->addWidget(m_files);

    auto *row = new QHBoxLayout;
    auto *attach = new QPushButton(tr("Attach…"), this);
    connect(attach, &QPushButton::clicked, this, &ComposeWindow::attach);
    auto *remove = new QPushButton(tr("Remove File"), this);
    remove->hide();
    connect(remove, &QPushButton::clicked, this, [this, remove] {
        const int chosen = m_files->currentRow();
        if (chosen < 0)
            return;
        m_attached.removeAt(chosen);
        delete m_files->takeItem(chosen);
        m_files->setVisible(!m_attached.isEmpty());
        remove->setVisible(!m_attached.isEmpty());
    });
    connect(m_files, &QListWidget::currentRowChanged, this, [remove](int r) { remove->setVisible(r >= 0); });
    m_status = new QLabel(this);
    m_send = new QPushButton(tr("Send"), this);
    m_send->setDefault(true);
    m_send->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_send->setToolTip(tr("Send (Ctrl+Enter)"));
    connect(m_send, &QPushButton::clicked, this, &ComposeWindow::send);
    row->addWidget(attach);
    row->addWidget(remove);
    row->addWidget(m_status, 1);
    row->addWidget(m_send);
    layout->addLayout(row);
    resize(720, 560);
    m_to->setFocus();
}

void ComposeWindow::setTo(const QString &to)
{
    m_to->setText(to);
}

void ComposeWindow::setCc(const QString &cc)
{
    m_cc->setText(cc);
}

void ComposeWindow::setSubject(const QString &subject)
{
    m_subject->setText(subject);
}

void ComposeWindow::setText(const QString &text)
{
    m_text->setPlainText(text);
    QTextCursor cursor = m_text->textCursor();
    cursor.movePosition(QTextCursor::Start);
    m_text->setTextCursor(cursor);
    if (!m_to->text().isEmpty())
        m_text->setFocus();
}

void ComposeWindow::setReplyTo(const QByteArray &messageId, const QByteArray &references)
{
    m_inReplyTo = messageId;
    m_references = references;
}

void ComposeWindow::addFile(const Composer::File &file)
{
    m_attached.append(file);
    m_files->addItem(QStringLiteral("%1  (%2)").arg(file.name, QLocale().formattedDataSize(file.data.size(), 1, QLocale::DataSizeSIFormat)));
    m_files->show();
}

void ComposeWindow::setSmtpSettings(const SmtpClient::Settings &settings)
{
    m_testSettings = settings;
    m_haveTestSettings = true;
}

Account ComposeWindow::account() const
{
    return Account::byId(m_from->currentData().toString());
}

Composer::Draft ComposeWindow::draft() const
{
    Composer::Draft d;
    const Account a = account();
    d.from = {a.name, a.email};
    d.to = addressesIn(m_to->text());
    d.cc = addressesIn(m_cc->text());
    d.bcc = addressesIn(m_bcc->text());
    d.subject = m_subject->text().trimmed();
    d.text = m_text->toPlainText();
    d.files = m_attached;
    d.inReplyTo = m_inReplyTo;
    d.references = m_references;
    return d;
}

void ComposeWindow::attach()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Attach Files"));
    qint64 total = 0;
    for (const Composer::File &f : std::as_const(m_attached))
        total += f.data.size();
    for (const QString &path : paths) {
        QFile file(path);
        if (file.size() + total > MaxAttachments) {
            QMessageBox::warning(this, tr("Attach Files"), tr("“%1” is too big: most mail servers take at most 25 MB a message.").arg(QFileInfo(path).fileName()));
            continue;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Attach Files"), tr("“%1” couldn't be read: %2").arg(path, file.errorString()));
            continue;
        }
        Composer::File f;
        f.name = QFileInfo(path).fileName();
        f.data = file.readAll();
        total += f.data.size();
        addFile(f);
    }
}

void ComposeWindow::setSending(bool sending)
{
    m_sending = sending;
    m_send->setEnabled(!sending);
    m_status->setText(sending ? tr("Sending…") : QString());
}

void ComposeWindow::send()
{
    if (m_sending)
        return;
    const Composer::Draft d = draft();
    const QStringList to = Composer::recipients(d);
    if (to.isEmpty()) {
        QMessageBox::warning(this, tr("Send"), tr("Enter at least one address to send to."));
        return;
    }
    const Account a = account();
    if (!a.isValid()) {
        QMessageBox::warning(this, tr("Send"), tr("Add an account first: File → Add Account."));
        return;
    }
    if (d.subject.isEmpty()
        && QMessageBox::question(this, tr("Send"), tr("Send without a subject?"), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
            != QMessageBox::Yes)
        return;
    const QByteArray raw = Composer::build(d);
    delete m_smtp;
    m_smtp = new SmtpClient(this);
    connect(m_smtp, &SmtpClient::sent, this, [this, raw, id = a.id] {
        m_done = true;
        setSending(false);
        emit sent(id, raw);
        close();
    });
    connect(m_smtp, &SmtpClient::failed, this, [this](const QString &message) {
        setSending(false);
        QMessageBox::warning(this, tr("Send"), tr("The message wasn't sent: %1").arg(message));
    });
    setSending(true);
    m_smtp->send(m_haveTestSettings ? m_testSettings : a.smtpSettings(), a.email, to, raw);
}

void ComposeWindow::closeEvent(QCloseEvent *event)
{
    if (m_done || (m_text->toPlainText().trimmed().isEmpty() && m_attached.isEmpty())) {
        event->accept();
        return;
    }
    if (m_sending) {
        event->ignore();
        return;
    }
    const auto answer = QMessageBox::question(this, tr("Close"), tr("Throw away this message?"), QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Discard)
        event->accept();
    else
        event->ignore();
}
