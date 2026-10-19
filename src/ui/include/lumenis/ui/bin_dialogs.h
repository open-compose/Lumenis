#pragma once

#include "lumenis/model/bin.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;

namespace lumenis::ui {

// The Bin Column Selection dialog (guide p.246): click a heading to select
// or deselect it, All/None, OK. Opened by BinView::chooseColumns(); the dialog
// only reports the choice, and the caller applies it with BinView::setColumns().
class ChooseColumnsDialog : public QDialog {
    Q_OBJECT

public:
    // `available`: every heading that can be shown; `shown`: those shown now.
    ChooseColumnsDialog(const QStringList& available, const QStringList& shown, QWidget* parent = nullptr);

    // The selected headings, in the order `available` gave them.
    QStringList chosen() const;
    // The heading list, for tests.
    QListWidget* list() const { return list_; }

private:
    QListWidget* list_ = nullptr;
};

// The Custom Sift dialog (p.288-289): two groups of three criteria, each a
// Criterion menu, Text to Find and Column or Range to Search; Clear, OK,
// Cancel and Apply. Opened by BinView::siftBinContents(); OK and Apply both
// hand the criteria back as a model::BinSift, which model::siftMatches() applies.
class CustomSiftDialog : public QDialog {
    Q_OBJECT

public:
    // Criteria rows in each of the two groups.
    static constexpr int kRows = 3;

    // `columns`: the headings the Column or Range to Search menu offers, after
    // "Any" (an empty column, which searches every shown column). `current`
    // fills the rows with the bin's present sift.
    CustomSiftDialog(const QStringList& columns, const model::BinSift& current, QWidget* parent = nullptr);

    // The criteria as entered. Rows with no text are left out, so a cleared
    // dialog gives an empty sift.
    model::BinSift sift() const;
    // Fills one row, for tests: `also` picks the second group, `row` is 0 to
    // kRows - 1, and a `column` the menu does not offer falls back to Any.
    void setRow(bool also, int row, const QString& text, const QString& column);

signals:
    // Apply: sift without closing. OK does not emit it; the caller reads
    // sift() after exec() instead.
    void applied(const model::BinSift& sift);

private:
    struct Row {
        QComboBox* criterion = nullptr;
        QLineEdit* text = nullptr;
        QComboBox* column = nullptr;
    };
    QList<Row> find_;
    QList<Row> also_;
};

// The Set Bin Display dialog (p.288): which object types the bin shows,
// "Show clips created by user" and "Show reference clips". Opened by
// BinView::setBinDisplay(), which copies the answers into model::BinDisplay.
class SetBinDisplayDialog : public QDialog {
    Q_OBJECT

public:
    // One check box per model::allKinds() entry, ticked unless `display`
    // hides that kind.
    SetBinDisplayDialog(const model::BinDisplay& display, QWidget* parent = nullptr);

    // The kinds left unticked.
    QList<model::BinItemKind> hiddenKinds() const;
    bool showUserClips() const;
    bool showReferenceClips() const;
    // The check box for `kind`, for tests; null if there is none.
    QCheckBox* box(model::BinItemKind kind) const;

private:
    QList<std::pair<model::BinItemKind, QCheckBox*>> kinds_;
    QCheckBox* userClips_ = nullptr;
    QCheckBox* referenceClips_ = nullptr;
};

} // namespace lumenis::ui
