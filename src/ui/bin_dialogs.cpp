#include "lumenis/ui/bin_dialogs.h"
#include "lumenis/ui/translation.h"

#include <QCheckBox>
#include <QComboBox>
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

// The bins' modal dialogs: Bin Column Selection, Custom Sift and Set Bin
// Display. They only collect a choice; BinView (bin_view.cpp) opens them and
// writes the result into the bin's model::BinDisplay (src/model/bin.h), where
// the filtering itself happens (BinFilterModel, model::siftMatches).

namespace lumenis::ui {

// ---------------------------------------------------------------- ChooseColumnsDialog

namespace {

// The heading list. Typing while it has the focus scrolls to the first shown heading that starts
// with what was typed (else contains it) and makes it current, without selecting it ("Typing in
// the column list scrolls to matching entries but does not select them", 2026.8 guide p.246).
// QAbstractItemView's own keyboard search would toggle the heading in a MultiSelection list.
class ColumnList : public QListWidget {
public:
    using QListWidget::QListWidget;

    void keyboardSearch(const QString& search) override {
        // Keys typed within the keyboard input interval add up, as in Qt's own search.
        if (!typed_.isValid() || typed_.elapsed() > QApplication::keyboardInputInterval()) text_.clear();
        typed_.start();
        text_ += search;
        QListWidgetItem* found = nullptr;
        for (const bool prefix : {true, false}) {
            for (int i = 0; i < count() && !found; ++i) {
                QListWidgetItem* it = item(i);
                if (it->isHidden()) continue;
                if (prefix ? it->text().startsWith(text_, Qt::CaseInsensitive)
                           : it->text().contains(text_, Qt::CaseInsensitive)) {
                    found = it;
                }
            }
            if (found) break;
        }
        if (!found) return;
        selectionModel()->setCurrentIndex(indexFromItem(found), QItemSelectionModel::NoUpdate);
        scrollToItem(found);
    }

private:
    QString text_;
    QElapsedTimer typed_;
};

} // namespace

ChooseColumnsDialog::ChooseColumnsDialog(const QStringList& available, const QStringList& shown, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("binColumnSelection"));
    setWindowTitle(tr("Bin Column Selection"));

    // The Search field at the top, focused when the dialog opens (2026.8 guide p.246; What's New
    // 2026.8 p.8): it shows only the headings that contain what is typed. Selections are kept
    // while headings are hidden.
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("columnSearch"));
    search_->setPlaceholderText(tr("Search"));
    search_->setClearButtonEnabled(true);

    list_ = new ColumnList(this);
    list_->setObjectName(QStringLiteral("columnList"));
    // A click selects a heading and a second click deselects it (p.246). The arrow keys move
    // between headings without selecting them; Space selects or deselects the current one.
    list_->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const QString& h : available) {
        // Shown in the language installed, kept by its heading (10.4b; names.json).
        auto* item = new QListWidgetItem(translatedResource("binColumns", h), list_);
        item->setData(Qt::UserRole, h);
        item->setSelected(shown.contains(h));
    }
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem* item = list_->item(i);
            item->setHidden(!item->text().contains(text.trimmed(), Qt::CaseInsensitive));
        }
    });

    auto* all = new QPushButton(tr("All/None"), this);
    all->setObjectName(QStringLiteral("allNone"));
    // All/None selects every heading shown unless they all are selected already (ours: with a
    // search, only the headings it shows).
    connect(all, &QPushButton::clicked, this, [this] {
        QList<QListWidgetItem*> shownItems;
        for (int i = 0; i < list_->count(); ++i) {
            if (!list_->item(i)->isHidden()) shownItems << list_->item(i);
        }
        const bool select = std::any_of(shownItems.begin(), shownItems.end(), [](QListWidgetItem* i) { return !i->isSelected(); });
        for (QListWidgetItem* i : shownItems) i->setSelected(select);
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addButton(all, QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Tab goes from the Search field to the list, Shift+Tab back (2026.8 guide p.246).
    setTabOrder(search_, list_);
    setTabOrder(list_, buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(search_);
    layout->addWidget(list_, 1);
    layout->addWidget(buttons);
    // It resizes vertically to show more headings (2026.8 guide p.246); the width stays.
    const int width = std::max(280, list_->sizeHintForColumn(0) + 2 * list_->frameWidth() +
                                        list_->style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 24);
    resize(width, 420);
    setMinimumHeight(200);
    setFixedWidth(width);
    search_->setFocus();
}

QLineEdit* ChooseColumnsDialog::search() const {
    return search_;
}

QStringList ChooseColumnsDialog::chosen() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->isSelected()) out << list_->item(i)->data(Qt::UserRole).toString();
    }
    return out;
}

// ---------------------------------------------------------------- CustomSiftDialog

CustomSiftDialog::CustomSiftDialog(const QStringList& columns, const model::BinSift& current, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("customSift"));
    setWindowTitle(tr("Custom Sift"));

    // Builds one group of kRows criteria rows into `rows`, filled from
    // `values` (the bin's current sift) as far as it goes. The column menu's
    // data is the heading, empty for Any.
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
                // A column no longer shown falls back to Any.
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
    // Clear empties the rows but neither closes nor applies.
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

// The rows with text, in order; the criterion is always Contains, so only the
// text and column are kept.
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

// ---------------------------------------------------------------- SetBinDisplayDialog

SetBinDisplayDialog::SetBinDisplayDialog(const model::BinDisplay& display, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("setBinDisplay"));
    setWindowTitle(tr("Set Bin Display"));
    auto* layout = new QVBoxLayout(this);
    // The object types of p.243-244.
    for (model::BinItemKind k : model::allKinds()) {
        auto* box = new QCheckBox(model::kindLabel(k), this);
        box->setChecked(!display.hiddenKinds.contains(k));
        layout->addWidget(box);
        kinds_.append({k, box});
    }
    layout->addSpacing(6);
    userClips_ = new QCheckBox(tr("Show clips created by user"), this);
    userClips_->setObjectName(QStringLiteral("showUserClips"));
    userClips_->setChecked(display.showUserClips);
    referenceClips_ = new QCheckBox(tr("Show reference clips"), this);
    referenceClips_->setObjectName(QStringLiteral("showReferenceClips"));
    referenceClips_->setChecked(display.showReferenceClips);
    layout->addWidget(userClips_);
    layout->addWidget(referenceClips_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QList<model::BinItemKind> SetBinDisplayDialog::hiddenKinds() const {
    QList<model::BinItemKind> out;
    for (const auto& [kind, box] : kinds_) {
        if (!box->isChecked()) out << kind;
    }
    return out;
}

bool SetBinDisplayDialog::showUserClips() const {
    return userClips_->isChecked();
}

bool SetBinDisplayDialog::showReferenceClips() const {
    return referenceClips_->isChecked();
}

QCheckBox* SetBinDisplayDialog::box(model::BinItemKind kind) const {
    for (const auto& [k, b] : kinds_) {
        if (k == kind) return b;
    }
    return nullptr;
}

} // namespace lumenis::ui
