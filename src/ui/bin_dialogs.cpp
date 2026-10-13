#include "lumenis/ui/bin_dialogs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace lumenis::ui {

// ---------------------------------------------------------------- ChooseColumnsDialog

ChooseColumnsDialog::ChooseColumnsDialog(const QStringList& available, const QStringList& shown, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("binColumnSelection"));
    setWindowTitle(tr("Bin Column Selection"));

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("columnList"));
    // A click selects a heading and a second click deselects it (p.246).
    list_->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const QString& h : available) {
        auto* item = new QListWidgetItem(h, list_);
        item->setSelected(shown.contains(h));
    }

    auto* all = new QPushButton(tr("All/None"), this);
    all->setObjectName(QStringLiteral("allNone"));
    connect(all, &QPushButton::clicked, this, [this] {
        const bool select = list_->selectedItems().size() < list_->count();
        for (int i = 0; i < list_->count(); ++i) list_->item(i)->setSelected(select);
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addButton(all, QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(list_, 1);
    layout->addWidget(buttons);
    resize(280, 420);
}

QStringList ChooseColumnsDialog::chosen() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->isSelected()) out << list_->item(i)->text();
    }
    return out;
}

// ---------------------------------------------------------------- CustomSiftDialog

CustomSiftDialog::CustomSiftDialog(const QStringList& columns, const model::BinSift& current, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("customSift"));
    setWindowTitle(tr("Custom Sift"));

    auto group = [&](const QString& title, QList<Row>& rows, const QList<model::SiftRow>& values) {
        auto* box = new QGroupBox(title, this);
        auto* grid = new QGridLayout(box);
        grid->addWidget(new QLabel(tr("Criterion:"), box), 0, 0);
        grid->addWidget(new QLabel(tr("Text to Find:"), box), 0, 1);
        grid->addWidget(new QLabel(tr("Column or Range to Search:"), box), 0, 2);
        for (int r = 0; r < kRows; ++r) {
            Row row;
            row.criterion = new QComboBox(box);
            // The only criterion the guide names (p.289).
            row.criterion->addItem(tr("Contains:"));
            row.text = new QLineEdit(box);
            row.column = new QComboBox(box);
            row.column->addItem(tr("Any"), QString());
            for (const QString& c : columns) row.column->addItem(c, c);
            if (r < values.size()) {
                row.text->setText(values.at(r).text);
                const int i = row.column->findData(values.at(r).column);
                row.column->setCurrentIndex(i < 0 ? 0 : i);
            }
            grid->addWidget(row.criterion, r + 1, 0);
            grid->addWidget(row.text, r + 1, 1);
            grid->addWidget(row.column, r + 1, 2);
            rows << row;
        }
        grid->setColumnStretch(1, 1);
        return box;
    };
    QWidget* findBox = group(tr("Find clips that meet these criteria:"), find_, current.find);
    QWidget* alsoBox = group(tr("Also find clips that meet these criteria:"), also_, current.also);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* clear = buttons->addButton(tr("Clear"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Ok);
    buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton* apply = buttons->addButton(QDialogButtonBox::Apply);
    connect(clear, &QPushButton::clicked, this, [this] {
        for (QList<Row>* rows : {&find_, &also_}) {
            for (Row& r : *rows) {
                r.text->clear();
                r.column->setCurrentIndex(0);
            }
        }
    });
    connect(apply, &QPushButton::clicked, this, [this] { emit applied(sift()); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(findBox);
    layout->addWidget(alsoBox);
    layout->addWidget(buttons);
}

model::BinSift CustomSiftDialog::sift() const {
    model::BinSift s;
    auto read = [](const QList<Row>& rows) {
        QList<model::SiftRow> out;
        for (const Row& r : rows) {
            if (!r.text->text().isEmpty()) out.append({r.text->text(), r.column->currentData().toString()});
        }
        return out;
    };
    s.find = read(find_);
    s.also = read(also_);
    return s;
}

void CustomSiftDialog::setRow(bool also, int row, const QString& text, const QString& column) {
    Row& r = (also ? also_ : find_)[row];
    r.text->setText(text);
    const int i = r.column->findData(column);
    r.column->setCurrentIndex(i < 0 ? 0 : i);
}

} // namespace lumenis::ui
