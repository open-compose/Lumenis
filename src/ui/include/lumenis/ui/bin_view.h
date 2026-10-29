#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/bin_lock.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QListView>
#include <QObject>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QJsonArray;
class QJsonValue;
class QSettings;
class QStackedWidget;
class QTableView;
class QToolButton;

// One open bin and the widgets that show it (guide p.245-294). BinDocument
// wraps a model::Bin (src/model/bin.h) and is the only thing that changes it;
// BinItemModel and BinFilterModel present it to Qt's views; BinView puts the
// Text, Frame and Script views together. BinContainer (bin_container.h) opens
// the documents and creates the views.

namespace lumenis::ui {

// An open bin: the bin's contents, where it is saved, and whether it has
// unsaved changes ("an asterisk appears before the bin name", guide p.258).
//
// Every change goes through a method here. Changes to the items emit
// itemsChanged(), changes to the display (columns, view, sift, frame size and
// so on) emit displayChanged(), and the first unsaved change emits
// dirtyChanged(true). Most methods do nothing, and emit nothing, when the
// change would leave the bin as it is.
class BinDocument : public QObject {
    Q_OBJECT

public:
    // `nodeId`: the bin's Sidebar id (a model::BinNode id or a Favorite Bin
    // id); `path`: its bin file (model::BinFile).
    BinDocument(QString nodeId, QString path, model::Bin bin, QObject* parent = nullptr);

    const QString& nodeId() const { return nodeId_; }
    const QString& path() const { return path_; }
    // After a rename moves the file; does not mark the bin changed.
    void setPath(const QString& path) { path_ = path; }
    const model::Bin& bin() const { return bin_; }
    QString name() const { return bin_.name; }
    // The name shown in the tab. Emits displayChanged() but does not mark the
    // bin changed: the name comes from the Sidebar, not from the file.
    void setName(const QString& name);
    bool isDirty() const { return dirty_; }

    void addItem(const model::BinItem& item);
    // Also forgets the removed items' Frame view positions.
    void removeItems(const QStringList& ids);
    // Replaces the item with the same id. Returns false if there is none.
    bool replaceItem(const model::BinItem& item);
    // Typing into a cell (p.278): Name, the modifiable headings and custom
    // columns. Returns false, changing nothing, for any other heading, an
    // empty name or an unchanged value.
    bool setField(const QString& id, const QString& heading, const QString& value);
    void setDisplay(const model::BinDisplay& display);

    // Bin > Sort (p.247); remembered for Sort Again. `keys` in priority
    // order; an empty list does nothing.
    void sort(const QList<model::BinSortKey>& keys);
    // Script view: dragging clips up or down (p.254). `toIndex` is the place
    // in the items before the move; the moved items keep their order.
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
    // Renames the column, its contents, width and sort keys. False if `from`
    // is not a custom column, or `to` is empty, unchanged or already taken.
    bool renameCustomColumn(const QString& from, const QString& to);

    // Writes the bin file (model::BinFile) and clears the dirty flag. On
    // failure returns false, sets `error` and stays dirty. A shared bin is
    // written only while its lock is still this user's (two users can both
    // see the green button for a moment, p.304); each save goes into the
    // bin's history (p.301).
    bool save(QString* error = nullptr);

    // Shared-storage locking (p.300-304, model/bin_lock.h). The states are
    // those of the Bin Lock Status button: green, this user holds the lock and
    // may change the bin; red, another user does (or the bin is protected);
    // yellow, the holder has saved changes since it was read; blue, the holder
    // has released the lock. A bin not on shared storage has no lock and no
    // button. BinContainer takes, releases and watches the locks.
    enum class LockState { NotShared, Mine, Others, Modified, Released, Protected };
    LockState lockState() const { return lockState_; }
    // Who holds the lock (Others, Modified, Protected, and Mine's own).
    const std::optional<model::BinLockHolder>& lockHolder() const { return lockHolder_; }
    void setLockState(LockState state, std::optional<model::BinLockHolder> holder = std::nullopt);
    // This user, for the history and for checking the lock before a save.
    void setLockUser(const model::BinLockHolder& me) { lockUser_ = me; }
    const std::optional<model::BinLockHolder>& lockUser() const { return lockUser_; }
    // Changes cannot be saved: someone else holds the lock, or the bin is
    // protected. Changes to the items are refused (writeRefused()); changes to
    // how it is shown are made but not marked as unsaved.
    bool isReadOnly() const;
    // The bin as read again from its file, replacing what is shown (a click on
    // the yellow button, p.302). Not marked changed.
    void reload(const model::Bin& bin);

signals:
    void itemsChanged();
    void displayChanged();
    void dirtyChanged(bool dirty);
    // save() wrote the file.
    void saved();
    void lockChanged();
    // A change to the items was refused: the bin is read-only.
    void writeRefused();

private:
    void touch();
    // True, emitting writeRefused(), when the bin is read-only.
    bool refused();

    QString nodeId_;
    QString path_;
    model::Bin bin_;
    bool dirty_ = false;
    LockState lockState_ = LockState::NotShared;
    std::optional<model::BinLockHolder> lockHolder_;
    std::optional<model::BinLockHolder> lockUser_;
};

// Rows of a bin: an icon column, Name, then the bin's chosen headings.
// One row per model::Bin item in the bin's order; the cell text comes from
// model::cellText(). Any change to the document resets the whole model.
class BinItemModel : public QAbstractTableModel {
    Q_OBJECT

public:
    static constexpr int kIconColumn = 0;
    static constexpr int kNameColumn = 1;
    static constexpr int kIdRole = Qt::UserRole + 1;
    static constexpr int kKindRole = Qt::UserRole + 2;  // model::BinItemKind
    static constexpr int kColorRole = Qt::UserRole + 3; // QColor, invalid for none
    static constexpr int kSummaryRole = Qt::UserRole + 4;

    // The document must outlive the model.
    explicit BinItemModel(BinDocument& document, QObject* parent = nullptr);

    // The heading of a column: empty for the icon column and for columns out
    // of range.
    QString heading(int column) const;
    // The column showing `heading`, or -1 if it is not shown.
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
    // Items from the bin `fromBin` (a node id) dropped here; `copy` as
    // isCopyDrop() (bin_dock.h) decides.
    void itemsDropped(const QString& fromBin, const QStringList& ids, bool copy);
    // Media files dropped from the Source Browser, to link (p.317).
    void filesDropped(const QStringList& paths);

private:
    BinDocument& document_;
    QStringList selectedHeadings_;
};

// The quick filter (p.294) and the sift (p.288-289). Also hides the objects
// Set Bin Display turns off (p.288). It filters only and never sorts: rows keep the
// bin's order, which BinDocument::sort() sets.
class BinFilterModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    BinFilterModel(BinDocument& document, QObject* parent = nullptr);
    // Case-insensitive "contains"; empty text shows every row. With headings selected, the quick filter searches them; otherwise
    // Name (p.294).
    void setQuickFilter(const QString& text, const QStringList& headings = {});
    // Bin settings (p.1153): with no column selected, search all columns
    // instead of Name. Shared by every bin; BinContainer sets it.
    static bool& searchAllColumns();

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    BinDocument& document_;
    QString quick_;
    QStringList quickHeadings_;
};

// Frame view (p.249-252): a frame per clip with its name below, moved
// freely, and an optional grid. Shows the Name column of BinView's filter
// model and shares the Text view's selection. Frames placed by hand are kept
// in model::BinDisplay::framePositions, in the view's content coordinates.
class BinFrameView : public QListView {
    Q_OBJECT

public:
    explicit BinFrameView(BinDocument& document, QWidget* parent = nullptr);

    // The frame size for a Frame view step (0 to kMaxFrameSize), in pixels,
    // 16:9. Steps out of range are clamped.
    static QSize frameSize(int step);
    // A grid cell: the bin's frame size plus margins and a line for the name.
    QSize cellSize() const;

    // Re-reads the size and the frames placed by hand.
    void applyDisplay();
    // Bin > Align and Fill (p.251). Align to Grid moves each frame (or only
    // the selected ones) to the nearest free cell; Fill Window spreads them
    // across the window in their order on screen; Fill Sorted forgets the
    // positions placed by hand and lays them out in the bin's order.
    void alignToGrid(bool selectedOnly);
    void fillWindow();
    void fillSorted();
    // Top-left of the item's cell, or (-1, -1) if it is not shown.
    QPoint framePosition(const QString& id) const;
    // A linked clip's head frame at `size` once decoded (p.250). A null image
    // until MediaFrames has it (thumbnailReady then repaints).
    QImage picture(const QString& id, QSize size) const;

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
// a text box for each. A column of plain widgets, one per row of `rows`
// (BinView's filter model), rebuilt whenever the rows change; the selection
// is the Text view's. Typing in a text box goes straight to the document.
class BinScriptView : public QScrollArea {
    Q_OBJECT

public:
    BinScriptView(BinDocument& document, QAbstractItemModel* rows, QItemSelectionModel* selection,
                  QWidget* parent = nullptr);

    // Recreates every row from the model.
    void rebuild();
    // The row widget for an item, or null if it is not shown.
    QWidget* rowFor(const QString& id) const;

signals:
    // Items dropped here from another bin. Drops from this bin reorder it
    // instead (BinDocument::moveItems).
    void itemsDropped(const QString& fromBin, const QStringList& ids, bool copy);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateSelection();
    void updatePictures();
    QPixmap scriptPicture(const model::BinItem& item, QSize size) const;
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
// menu above the view, and an optional status bar. The three views share one
// The Interface settings' Bins tab (p.1201), for every bin: whether bins show their own background
// colour (Allow custom bin backgrounds), and only on their tabs; and a font and size that override
// each bin's own (Override all bin fonts, Override all bin font sizes). BinContainer sets it.
//
// From the Bin settings (p.1154): Frame view borders coloured by object type or by clip colour, and
// the object icons on the frames; and Show local media icons, which colours a clip's icon by what
// `mediaTint` says (MainWindow gives it the media: blue for local, pink for mixed resolution).
struct BinAppearance {
    bool backgrounds = true;
    bool tabsOnly = false;
    QString fontFamily; // empty: each bin's own
    int fontSize = 0;   // 0: each bin's own
    enum class FrameBorders { Type, ClipColor };
    FrameBorders frameBorders = FrameBorders::Type;
    bool frameIcons = true;
    bool localMediaIcons = false;
    bool operator==(const BinAppearance&) const = default;
    static BinAppearance& current();
    // The icon colour for a clip with Show local media icons; an invalid colour leaves it as it is.
    static std::function<QColor(const model::BinItem&)>& mediaTint();
    // A frame's border colour in the Frame view by object type (p.1154); invalid for no border.
    static QColor typeColor(model::BinItemKind kind);
};

// BinFilterModel and one selection model, so a selection survives switching
// views. Everything the view changes goes through the BinDocument, and the
// view redraws itself from displayChanged().
class BinView : public QWidget {
    Q_OBJECT

public:
    // File > Input, offered in the fast menu and on right-click (p.315).
    // Redraws after BinAppearance::current() changed.
    void refreshAppearance();
    void setInputMenu(QMenu* menu) { inputMenu_ = menu; }
    // `fastMenu` is the Bin menu, also offered from the bin (p.259); its
    // actions are shared, not copied, and it may be null. The document must
    // outlive the view.
    BinView(BinDocument& document, QMenu* fastMenu, QWidget* parent = nullptr);

    BinDocument& document() const { return document_; }
    QTableView* table() const { return table_; }
    BinFrameView* frameView() const { return frames_; }
    BinScriptView* scriptView() const { return script_; }
    QComboBox* viewMenu() const { return viewMenu_; }
    BinItemModel* itemModel() const { return model_; }

    // Selected item ids, in the order shown.
    QStringList selectedIds() const;
    // Replaces the selection; ids that are not shown are ignored.
    void selectIds(const QStringList& ids);
    // Every item shown (not those filtered or sifted out).
    void selectAll();
    // Bin > Select > Reverse (p.259).
    void reverseSelection();
    // Filtering items (p.294): matches the Name column, or in Text view the
    // selected headings (see BinFilterModel::setQuickFilter).
    void setQuickFilter(const QString& text);
    const QString& quickFilter() const { return quick_; }
    // Rows shown after Set Bin Display, the quick filter and the sift.
    int visibleCount() const;
    // Ids in the order shown (sorted and sifted).
    QStringList visibleIds() const;

    // The Text, Frame and Script view buttons (p.245-254). Keeps the selection.
    void setMode(model::BinViewMode mode);
    model::BinViewMode mode() const { return document_.bin().display.mode; }

    // The Bin View menu (p.248-249): a built-in or saved view, or Save As.
    // applyView() sets the bin's columns to the view's; an unknown name does
    // nothing. saveViewAs() stores the bin's columns in BinViewLibrary,
    // replacing a saved view of the same name.
    void applyView(const QString& name);
    void saveViewAs(const QString& name);
    // Refills the Bin View menu: built-in views, saved views, Save As. A bin
    // whose view is not in the list (or has none, "Untitled") gets an extra
    // entry at the top for it; a modified view's entry is in italic.
    void updateViewMenu();

    // Columns (p.246-247, 268-269). Selected headings are clicked ones.
    QStringList selectedHeadings() const { return selectedHeadings_; }
    // Headings that are not shown are dropped. Re-runs the quick filter,
    // which searches the selected headings.
    void selectHeadings(const QStringList& headings);
    // Opens the Bin Column Selection dialog.
    void chooseColumns();
    // Shows exactly `headings` after Name, new ones beside the selected
    // column or at the far right (p.247).
    // Changing the columns marks the bin's view modified (p.249).
    void setColumns(const QStringList& headings);
    void hideColumns(const QStringList& headings);
    // Edit > Delete on columns: standard ones hide; custom ones are deleted
    // with their contents after asking (p.268).
    void deleteColumns(const QStringList& headings, bool ask = true);
    // "Untitled" (p.269), or "Untitled.<n>" if that is taken; returns the
    // name. In a visible Text view its heading opens for typing.
    QString addCustomColumn();
    // Custom columns only; see BinDocument::renameCustomColumn.
    bool renameColumn(const QString& from, const QString& to);
    // Bin > Align and Fill > Align Columns: each column as wide as its
    // contents, except Color.
    void alignColumns();

    // Sorting (p.247-248). Each keeps the selection. sortSelected() returns
    // false if no heading is selected; Sort Again repeats the bin's last sort.
    void sortOn(const QString& heading, bool ascending);
    bool sortSelected(); // Bin > Sort: the selected headings, left to right
    void sortAgain();

    // Sifting (p.288-289).
    void siftBinContents(); // the Custom Sift dialog
    // Sets the sift and shows the bin sifted; an empty sift shows it unsifted.
    void sift(const model::BinSift& sift);
    // Sift Selected Items: only the selected items stay.
    void siftSelected();
    // Show Sifted / Show Unsifted; does nothing while the bin has no sift.
    void setSiftShown(bool shown);

    // Frame view (p.250-252): one step larger or smaller, within 0 to
    // kMaxFrameSize, and the Snap to Grid setting.
    void enlargeFrame();
    void reduceFrame();
    void setSnapToGrid(model::SnapToGrid snap);

    // Colours (p.263-264) and locks (p.265) for the selected items; colours
    // are "#rrggbb" names and an empty one is None. The background is the
    // whole bin's.
    void setClipColor(const QString& color);
    void setBackground(const QString& color);
    void setLocked(bool locked);

    // Bin > Set Bin Display (p.288): opens the dialog; setHiddenKinds() sets
    // the hidden kinds directly.
    void setBinDisplay();
    void setHiddenKinds(const QList<model::BinItemKind>& kinds);

    // The bin's right-click menu at `viewportPos` in `view` (the table, the
    // Frame view or the Script view). The caller owns the menu and deletes it.
    // Right-clicking an unselected row selects it first.
    QMenu* contextMenu(const QPoint& viewportPos, QWidget* view);

signals:
    // Delete or Backspace in the bin with no headings selected; BinContainer
    // deletes the selected items. (With headings selected the columns are
    // deleted here instead.)
    void deleteRequested();
    // Items dropped here from another bin (p.260): moved, or with the copy
    // modifier copied.
    void itemsDropped(const QString& fromBin, const QStringList& ids, bool copy);
    // Media files dropped from the Source Browser, to link (p.317).
    void filesDropped(const QStringList& paths);
    // Double-click on an item: it loads into a monitor (p.394).
    void itemActivated(const QString& itemId);
    // saveViewAs() changed BinViewLibrary.
    void viewSaved();
    // The Bin Lock Status button was clicked, `alt` with Alt (Option) held
    // (p.302-303); BinContainer::lockButtonClicked() acts on it.
    void lockButtonClicked(bool alt);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyDisplay();
    void updateStatus();
    // The Bin Lock Status button's colour and tooltip, from the document.
    void updateLockButton();
    void headerClicked(int logical);
    void headerMenu(const QPoint& pos);
    void editHeading(const QString& heading);
    void columnsMoved();
    QMenu* colorMenu(QMenu* parent, const QString& title, bool background);

    BinDocument& document_;
    QMenu* inputMenu_ = nullptr;
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
    QToolButton* lockButton_ = nullptr;
    QStringList selectedHeadings_;
    QStringList keepSelection_;
    QString quick_;
    bool applying_ = false;
};

// Bin views saved with Save As (p.249). They belong to the user, not to a
// bin: the guide keeps them as User settings (p.249). MainWindow keeps them
// in the active Bin setting's "binViews" value, reading it in
// applySettings() and writing it on BinContainer::binViewsChanged().
class BinViewLibrary {
public:
    // The saved views, process-wide; built-in views are not in the list
    // (model::builtInBinViews()).
    static QList<model::BinViewPreset>& views();
    // A saved view by name, or null.
    static const model::BinViewPreset* find(const QString& name);
    // The views as a JSON array of {name, columns}, and back. fromJson()
    // skips entries without a name, and returns whether the list changed.
    static QJsonArray toJson();
    static bool fromJson(const QJsonValue& value);
    // The views as builds before they were a User setting kept them, in the
    // application's QSettings: as toJson() makes them, null when there are
    // none. forgetLegacy() removes them once MainWindow has adopted them.
    static QJsonValue legacy(QSettings& settings);
    static void forgetLegacy(QSettings& settings);
};

} // namespace lumenis::ui
