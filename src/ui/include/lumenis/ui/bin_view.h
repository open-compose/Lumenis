#pragma once

#include "lumenis/model/bin.h"

#include <QAbstractTableModel>
#include <QListView>
#include <QObject>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QSettings;
class QStackedWidget;
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
    // Replaces the item with the same id. Returns false if there is none.
    bool replaceItem(const model::BinItem& item);
    // Typing into a cell (p.278): Name, the modifiable headings and custom
    // columns.
    bool setField(const QString& id, const QString& heading, const QString& value);
    void setDisplay(const model::BinDisplay& display);

    // Bin > Sort (p.247); remembered for Sort Again.
    void sort(const QList<model::BinSortKey>& keys);
    // Script view: dragging clips up or down (p.254).
    void moveItems(const QStringList& ids, int toIndex);
    // Set Clip Color (p.264); an empty colour is None.
    void setColor(const QStringList& ids, const QString& color);
    // Clip > Lock/Unlock Bin Selection (p.265).
    void setLocked(const QStringList& ids, bool locked);
    // Changes that the views already show, so nothing is redrawn: the Script
    // view's text, column widths and frames placed by hand.
    void setScript(const QString& id, const QString& text);
    void setColumnWidth(const QString& heading, int width);
    void setFramePositions(const QHash<QString, QPoint>& positions);
    // Custom columns (p.268-269): deleting one deletes what was typed in it.
    void deleteCustomColumn(const QString& heading);
    bool renameCustomColumn(const QString& from, const QString& to);

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

// Rows of a bin: an icon column, Name, then the bin's chosen headings.
class BinItemModel : public QAbstractTableModel {
    Q_OBJECT

public:
    static constexpr int kIconColumn = 0;
    static constexpr int kNameColumn = 1;
    static constexpr int kIdRole = Qt::UserRole + 1;
    static constexpr int kKindRole = Qt::UserRole + 2;  // model::BinItemKind
    static constexpr int kColorRole = Qt::UserRole + 3; // QColor, invalid for none
    static constexpr int kSummaryRole = Qt::UserRole + 4;

    explicit BinItemModel(BinDocument& document, QObject* parent = nullptr);

    QString heading(int column) const;
    int columnOf(const QString& heading) const;
    // Headings picked by clicking them (p.247), shown highlighted.
    void setSelectedHeadings(const QStringList& headings);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Items dragged out carry their bin; items from another bin dropped
    // here are reported, and the Bin Container moves or copies them.
    Qt::DropActions supportedDragActions() const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

signals:
    void itemsDropped(const QString& fromBin, const QStringList& ids, bool copy);

private:
    BinDocument& document_;
    QStringList selectedHeadings_;
};

// The quick filter (p.294) and the sift (p.288-289).
class BinFilterModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    BinFilterModel(BinDocument& document, QObject* parent = nullptr);
    // With headings selected, the quick filter searches them; otherwise
    // Name (p.294).
    void setQuickFilter(const QString& text, const QStringList& headings = {});
    // Bin settings (p.1153): with no column selected, search all columns
    // instead of Name.
    static bool& searchAllColumns();

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    BinDocument& document_;
    QString quick_;
    QStringList quickHeadings_;
};

// Frame view (p.249-252): a frame per clip with its name below, moved
// freely, and an optional grid.
class BinFrameView : public QListView {
    Q_OBJECT

public:
    explicit BinFrameView(BinDocument& document, QWidget* parent = nullptr);

    // The frame size for a Frame view step (0 to kMaxFrameSize).
    static QSize frameSize(int step);
    QSize cellSize() const;

    // Re-reads the size and the frames placed by hand.
    void applyDisplay();
    // Bin > Align and Fill (p.251).
    void alignToGrid(bool selectedOnly);
    void fillWindow();
    void fillSorted();
    QPoint framePosition(const QString& id) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool viewportEvent(QEvent* event) override;

public:
    // After every layout, frames placed by hand go back where they were.
    void doItemsLayout() override;

private:
    void placeFrames(const QList<std::pair<QModelIndex, QPoint>>& places);

    void storePositions(const QModelIndexList& moved);
    QPoint snapped(const QPoint& p) const;

    BinDocument& document_;
};

// Script view (p.252-254): frames down the left, the clip's details above
// a text box for each.
class BinScriptView : public QScrollArea {
    Q_OBJECT

public:
    BinScriptView(BinDocument& document, QAbstractItemModel* rows, QItemSelectionModel* selection,
                  QWidget* parent = nullptr);

    void rebuild();
    QWidget* rowFor(const QString& id) const;

signals:
    // Items dropped here from another bin.
    void itemsDropped(const QString& fromBin, const QStringList& ids, bool copy);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateSelection();
    QString idAt(QObject* rowWidget) const;

    BinDocument& document_;
    QAbstractItemModel* rows_;
    QItemSelectionModel* selection_;
    QWidget* content_ = nullptr;
    QPoint pressPos_;
    QString pressId_;
};

// One bin as shown in the Bin Container (p.245-257): the Bin View menu,
// the Text, Frame and Script view buttons, the quick filter and the fast
// menu above the view, and an optional status bar.
class BinView : public QWidget {
    Q_OBJECT

public:
    // `fastMenu` is the Bin menu, also offered from the bin (p.259).
    BinView(BinDocument& document, QMenu* fastMenu, QWidget* parent = nullptr);

    BinDocument& document() const { return document_; }
    QTableView* table() const { return table_; }
    BinFrameView* frameView() const { return frames_; }
    BinScriptView* scriptView() const { return script_; }
    QComboBox* viewMenu() const { return viewMenu_; }
    BinItemModel* itemModel() const { return model_; }

    QStringList selectedIds() const;
    void selectIds(const QStringList& ids);
    void selectAll();
    // Bin > Select > Reverse (p.259).
    void reverseSelection();
    // Filtering items (p.294): matches the Name column.
    void setQuickFilter(const QString& text);
    const QString& quickFilter() const { return quick_; }
    int visibleCount() const;
    // Ids in the order shown (sorted and sifted).
    QStringList visibleIds() const;

    // The Text, Frame and Script view buttons (p.245-254).
    void setMode(model::BinViewMode mode);
    model::BinViewMode mode() const { return document_.bin().display.mode; }

    // The Bin View menu (p.248-249): a built-in or saved view, or Save As.
    void applyView(const QString& name);
    void saveViewAs(const QString& name);

    // Columns (p.246-247, 268-269). Selected headings are clicked ones.
    QStringList selectedHeadings() const { return selectedHeadings_; }
    void selectHeadings(const QStringList& headings);
    void chooseColumns();
    // Shows exactly `headings` after Name, new ones beside the selected
    // column or at the far right (p.247).
    void setColumns(const QStringList& headings);
    void hideColumns(const QStringList& headings);
    // Edit > Delete on columns: standard ones hide; custom ones are deleted
    // with their contents after asking (p.268).
    void deleteColumns(const QStringList& headings, bool ask = true);
    QString addCustomColumn(); // "Untitled" (p.269)
    bool renameColumn(const QString& from, const QString& to);
    void alignColumns();

    // Sorting (p.247-248).
    void sortOn(const QString& heading, bool ascending);
    bool sortSelected(); // Bin > Sort: the selected headings, left to right
    void sortAgain();

    // Sifting (p.288-289).
    void siftBinContents(); // the Custom Sift dialog
    void sift(const model::BinSift& sift);
    void siftSelected();
    void setSiftShown(bool shown);

    // Frame view (p.250-252).
    void enlargeFrame();
    void reduceFrame();
    void setSnapToGrid(model::SnapToGrid snap);

    // Colours (p.263-264) and locks (p.265) for the selected items.
    void setClipColor(const QString& color);
    void setBackground(const QString& color);
    void setLocked(bool locked);

    // Bin > Set Bin Display (p.288).
    void setBinDisplay();
    void setHiddenKinds(const QList<model::BinItemKind>& kinds);

    // The bin's right-click menu.
    QMenu* contextMenu(const QPoint& viewportPos, QWidget* view);

signals:
    // Delete or Backspace in the bin.
    void deleteRequested();
    // Items dropped here from another bin (p.260): moved, or with the copy
    // modifier copied.
    void itemsDropped(const QString& fromBin, const QStringList& ids, bool copy);
    // Double-click on an item: it loads into a monitor (p.394).
    void itemActivated(const QString& itemId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyDisplay();
    void updateStatus();
    void updateViewMenu();
    void headerClicked(int logical);
    void headerMenu(const QPoint& pos);
    void editHeading(const QString& heading);
    void columnsMoved();
    QMenu* colorMenu(QMenu* parent, const QString& title, bool background);

    BinDocument& document_;
    QMenu* fastMenu_ = nullptr;
    BinItemModel* model_ = nullptr;
    BinFilterModel* proxy_ = nullptr;
    QComboBox* viewMenu_ = nullptr;
    QButtonGroup* modes_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QTableView* table_ = nullptr;
    BinFrameView* frames_ = nullptr;
    BinScriptView* script_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QLabel* status_ = nullptr;
    QStringList selectedHeadings_;
    QStringList keepSelection_;
    QString quick_;
    bool applying_ = false;
};

// Bin views saved with Save As (p.249). They belong to the user, not to a
// bin, and are kept in the application settings.
class BinViewLibrary {
public:
    static QList<model::BinViewPreset>& views();
    static const model::BinViewPreset* find(const QString& name);
    static void load(QSettings& settings);
    static void save(QSettings& settings);
};

} // namespace lumenis::ui
