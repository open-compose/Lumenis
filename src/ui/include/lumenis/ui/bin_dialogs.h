#pragma once

#include "lumenis/model/bin.h"

#include <QDialog>
#include <QList>

class QComboBox;
class QLineEdit;
class QListWidget;

namespace lumenis::ui {

// The Bin Column Selection dialog (guide p.246): click a heading to select
// or deselect it, All/None, OK.
class ChooseColumnsDialog : public QDialog {
    Q_OBJECT

public:
    // `available`: every heading that can be shown; `shown`: those shown now.
    ChooseColumnsDialog(const QStringList& available, const QStringList& shown, QWidget* parent = nullptr);

    QStringList chosen() const;
    QListWidget* list() const { return list_; }

private:
    QListWidget* list_ = nullptr;
};

// The Custom Sift dialog (p.288-289): two groups of three criteria, each a
// Criterion menu, Text to Find and Column or Range to Search; Clear, OK,
// Cancel and Apply.
class CustomSiftDialog : public QDialog {
    Q_OBJECT

public:
    static constexpr int kRows = 3;

    // `columns`: the headings the Column or Range to Search menu offers.
    CustomSiftDialog(const QStringList& columns, const model::BinSift& current, QWidget* parent = nullptr);

    model::BinSift sift() const;
    void setRow(bool also, int row, const QString& text, const QString& column);

signals:
    // Apply: sift without closing.
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

} // namespace lumenis::ui
