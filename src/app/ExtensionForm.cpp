#include "ExtensionForm.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace ExtensionForm {

Answer ask(QWidget *window, const Extensions::Form &form)
{
    using Extensions::FormField;
    Answer answer;
    QPointer<QDialog> dialog = new QDialog(window);
    dialog->setObjectName("connectDialog");
    dialog->setWindowTitle(form.title.isEmpty() ? form.extensionName : form.title);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(8);

    // Everything the extension wrote is shown as plain text: it can't put
    // links or pictures in a dialog of ours.
    auto addLabel = [&](const QString &text, const char *name) {
        if (text.isEmpty())
            return;
        auto *label = new QLabel(text, dialog);
        label->setObjectName(QString::fromLatin1(name));
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        layout->addWidget(label);
    };
    addLabel(form.heading, "prefHeading");
    addLabel(form.text, "prefHint");
    addLabel(form.warning, "propDanger");

    struct Shown
    {
        FormField field;
        QWidget *widget;
    };
    QList<Shown> shown;
    auto *fields = new QFormLayout;
    fields->setContentsMargins(0, 6, 0, 0);
    QWidget *first = nullptr;
    for (const FormField &field : form.fields) {
        QWidget *widget = nullptr;
        switch (field.kind) {
        case FormField::Check: {
            auto *box = new QCheckBox(field.label, dialog);
            box->setObjectName("prefCheck");
            box->setChecked(field.value == QLatin1String("true"));
            fields->addRow(box);
            widget = box;
            break;
        }
        case FormField::Choice: {
            auto *combo = new QComboBox(dialog);
            combo->addItems(field.choices);
            combo->setCurrentIndex(qMax(0, int(field.choices.indexOf(field.value))));
            fields->addRow(field.label, combo);
            widget = combo;
            break;
        }
        case FormField::Text:
            if (!field.choices.isEmpty()) {
                // Suggestions (servers used before) to pick, or type over.
                auto *combo = new QComboBox(dialog);
                combo->setEditable(true);
                combo->setInsertPolicy(QComboBox::NoInsert);
                combo->addItems(field.choices);
                combo->setCurrentIndex(-1);
                combo->setEditText(field.value);
                combo->lineEdit()->setPlaceholderText(field.placeholder);
                combo->setMinimumWidth(320);
                fields->addRow(field.label, combo);
                widget = combo;
                break;
            }
            [[fallthrough]];
        case FormField::Secret: {
            auto *edit = new QLineEdit(field.kind == FormField::Secret ? QString() : field.value, dialog);
            edit->setObjectName("connectAddress");
            edit->setPlaceholderText(field.placeholder);
            if (field.kind == FormField::Secret)
                edit->setEchoMode(QLineEdit::Password);
            edit->setMinimumWidth(280);
            fields->addRow(field.label, edit);
            widget = edit;
            break;
        }
        }
        shown.append({field, widget});
        // The first thing still to fill in gets the keyboard.
        if (!first && field.kind != FormField::Check && (field.value.isEmpty() || field.kind == FormField::Secret))
            first = widget;
    }
    if (!shown.isEmpty())
        layout->addLayout(fields);
    // A tick box that stands alone ("Connect anonymously") turns the rest off.
    for (const Shown &entry : std::as_const(shown)) {
        if (entry.field.kind != FormField::Check || !entry.field.alone)
            continue;
        auto *box = static_cast<QCheckBox *>(entry.widget);
        auto apply = [shown, box] {
            for (const Shown &other : shown) {
                if (other.widget != box)
                    other.widget->setEnabled(!box->isChecked());
            }
        };
        QObject::connect(box, &QCheckBox::toggled, dialog, apply);
        apply();
    }

    // Who is asking: a password typed here is handed to that extension.
    auto *from = new QLabel(QObject::tr("Asked by the extension “%1”.").arg(form.extensionName), dialog);
    from->setObjectName("prefHint");
    from->setTextFormat(Qt::PlainText);
    layout->addSpacing(4);
    layout->addWidget(from);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
    const QStringList labels = form.buttons.isEmpty() ? QStringList{QObject::tr("OK")} : form.buttons;
    int pressed = 0;
    for (int i = 0; i < labels.size(); ++i) {
        // A name is a name: "&" isn't a shortcut key.
        QPushButton *button = buttons->addButton(QString(labels.at(i)).replace(QLatin1Char('&'), QStringLiteral("&&")),
                                                 QDialogButtonBox::AcceptRole);
        // With fields to fill in, Enter is the first button; a bare
        // question (an unknown host key) has no default: it wants a click.
        button->setAutoDefault(!shown.isEmpty());
        button->setDefault(i == 0 && !shown.isEmpty());
        QObject::connect(button, &QPushButton::clicked, dialog, [&pressed, i, dialog] {
            pressed = i;
            dialog->accept();
        });
    }
    if (shown.isEmpty())
        buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addSpacing(8);
    layout->addWidget(buttons);
    if (first)
        first->setFocus();
    dialog->resize(qMax(440, dialog->sizeHint().width()), dialog->sizeHint().height());

    const bool accepted = dialog->exec() == QDialog::Accepted;
    if (!dialog)
        return answer; // the window went away meanwhile
    if (accepted) {
        answer.cancelled = false;
        answer.button = pressed;
        for (const Shown &entry : std::as_const(shown)) {
            QString value;
            if (auto *box = qobject_cast<QCheckBox *>(entry.widget))
                value = box->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
            else if (auto *combo = qobject_cast<QComboBox *>(entry.widget))
                value = combo->currentText();
            else if (auto *edit = qobject_cast<QLineEdit *>(entry.widget))
                value = edit->text();
            answer.values.insert(entry.field.id, value);
        }
    }
    delete dialog;
    return answer;
}

} // namespace ExtensionForm
