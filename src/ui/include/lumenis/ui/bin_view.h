#pragma once

#include "lumenis/model/bin.h"

#include <QAbstractTableModel>
#include <QObject>
#include <QWidget>

class QLabel;
class QLineEdit;
class QMenu;
class QSortFilterProxyModel;
class QTableView;
class QToolButton;

namespace lumenis::ui {

// An open bin: the bin's contents, where it is saved, and whether it has
// unsaved changes ("an asterisk appears before the bin name", guide p.258).
class BinDocument : public QObject {
    Q_OBJECT

public:
    BinDocument(QString nodeId, QString path, model::Bin bin, QObject* parent = nullptr);

    const QString& nodeId() const { return nodeId_; }
    const QString& path() const { return path_; }
    void setPath(const QString& path) { path_ = path; }
    const model::Bin& bin() const { return bin_; }
    QString name() const { return bin_.name; }
    void setName(const QString& name);
    bool isDirty() const { return dirty_; }

    void addItem(const model::BinItem& item);
    void removeItems(const QStringList& ids);
    // Name and custom columns can be typed into (p.278).
    bool setField(const QString& id, const QString& heading, const QString& value);
    void setDisplay(const model::BinDisplay& display);

    bool save(QString* error = nullptr);

signals:
    void itemsChanged();
    void displayChanged();
    void dirtyChanged(bool dirty);

private:
    void touch();

    QString nodeId_;
    QString path_;
    model::Bin bin_;
    bool dirty_ = false;
};

// Text view rows: an icon column, Name, then the bin's chosen headings.
class BinItemModel : public QAbstractTableModel {
    Q_OBJECT

public:
    static constexpr int kIconColumn = 0;
    static constexpr int kNameColumn = 1;
    static constexpr int kIdRole = Qt::UserRole + 1;

    explicit BinItemModel(BinDocument& document, QObject* parent = nullptr);

    QString heading(int column) const;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    BinDocument& document_;
};

// One bin as shown in the Bin Container (p.245-257): the Bin View menu,
// the Text, Frame and Script view buttons, the quick filter and the fast
// menu above the Text view, and an optional status bar.
class BinView : public QWidget {
    Q_OBJECT

public:
    // `fastMenu` is the Bin menu, also offered from the bin (p.259).
    BinView(BinDocument& document, QMenu* fastMenu, QWidget* parent = nullptr);

    BinDocument& document() const { return document_; }
    QTableView* table() const { return table_; }

    QStringList selectedIds() const;
    void selectIds(const QStringList& ids);
    void selectAll();
    // Filtering items (p.294): matches the Name column.
    void setQuickFilter(const QString& text);
    int visibleCount() const;

signals:
    // Delete or Backspace in the bin.
    void deleteRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyDisplay();
    void updateStatus();

    BinDocument& document_;
    BinItemModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace lumenis::ui
