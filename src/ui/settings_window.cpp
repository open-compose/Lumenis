#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/target_settings_dialog.h"

#include "lumenis/model/safe_colors.h"
#include "lumenis/ui/safe_color_graph.h"

#include "lumenis/core/branding.h"
#include "lumenis/core/timecode.h"
#include "lumenis/media/managed.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

// The Settings window and its parts (settings_window.h): SettingsDialog, the generic dialog built
// from a setting's options in src/ui/resources/settings.json; SettingsList, the Settings list of one
// scope (Project, User or Site) with Duplicate, Delete, Restore to Default, renaming, the active check
// boxes and drag-and-drop between lists; SettingsWindow (File > Settings) and SiteSettingsWindow; and
// CreateUserProfileDialog.
//
// The lists edit the model::SettingsFile objects held by ui::Settings (settings.cpp) and save them with
// Settings::commit(), which emits Settings::changed for every window to re-read its options. User
// profile folders come from model::UserProfiles (src/model). MainWindow creates the windows and opens
// the tools that some settings stand for (toolRequested).

namespace lumenis::ui {

using model::SettingEntry;
using model::SettingsFile;
using model::SettingsScope;

namespace {

QString laterTip() {
    return QObject::tr("Not available yet");
}

// Only the Name column is edited in place (p.1139).
class NameDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (index.column() != SettingsList::kNameColumn) return nullptr;
        return QStyledItemDelegate::createEditor(parent, option, index);
    }
};

// Settings entries as the "settings" array of a settings file, for drag data.
QJsonArray entriesJson(const QList<SettingEntry>& entries) {
    SettingsFile f;
    f.entries = entries;
    return f.toJson().value(QStringLiteral("settings")).toArray();
}

// The entries in drag data from entriesJson(), read back through SettingsFile::fromJson; empty if it
// does not parse.
QList<SettingEntry> entriesFrom(const QMimeData* mime) {
    const QJsonDocument doc = QJsonDocument::fromJson(mime->data(SettingsList::mimeType()));
    QJsonObject o = SettingsFile().toJson();
    o.insert(QStringLiteral("settings"), doc.object().value(QStringLiteral("settings")));
    const auto f = SettingsFile::fromJson(o);
    return f ? f->entries : QList<SettingEntry>{};
}

} // namespace

// ---------------------------------------------------------------- SettingsDialog

SettingsDialog::SettingsDialog(const SettingKind& kind, const SettingEntry& entry, QWidget* parent)
    : QDialog(parent), kind_(kind), values_(entry.values) {
    setObjectName(QStringLiteral("settingsDialog"));
    // "Bin Settings - Current" in Media Composer; the custom name here.
    setWindowTitle(entry.name.isEmpty() ? kind.title() : QStringLiteral("%1 - %2").arg(kind.title(), entry.name));

    // An option's starting value: the entry's, or the default where Settings::value() would use it
    // (none stored, not available yet, the wrong type, an unknown choice).
    auto value = [&](const SettingOption& o) { return o.effectiveValue(values_.value(o.key)); };
    // One tab's page: a row per option, with the control its type and style call for. Every control
    // is named after its option key and recorded in editors_.
    auto page = [&](const SettingTab& tab) {
        auto* w = new QWidget;
        auto* v = new QVBoxLayout(w);
        for (const SettingOption& o : tab.options) {
            QWidget* editor = nullptr;
            QWidget* row = nullptr;
            const QJsonValue current = value(o);
            if (o.type == QLatin1String("bool")) {
                auto* box = new QCheckBox(o.label);
                box->setChecked(current.toBool());
                editor = row = box;
            } else if (o.type == QLatin1String("safeColorGraph")) {
                // The Safe Colors settings' graph (FX Guide p.722-723), following the dialog's limits as they change.
                auto* graph = new SafeColorGraph;
                graph->setLimitsSource([this] { return model::SafeColorLimits::fromValues(values()); });
                editor = row = graph;
            } else if (o.type == QLatin1String("choice") && o.style == QLatin1String("radio")) {
                // Radio buttons in a box titled with the label; the QButtonGroup "<key>Group" ids
                // are indices into o.choices, which values() reads back.
                QWidget* holder = o.label.isEmpty() ? new QWidget : new QGroupBox(o.label);
                auto* buttons = new QVBoxLayout(holder);
                auto* group = new QButtonGroup(holder);
                group->setObjectName(o.key + QStringLiteral("Group"));
                for (int i = 0; i < o.choices.size(); ++i) {
                    const SettingChoice& c = o.choices.at(i);
                    auto* r = new QRadioButton(c.label, holder);
                    r->setObjectName(o.key + QLatin1Char(':') + c.value);
                    r->setChecked(c.value == current.toString());
                    if (c.later) {
                        r->setEnabled(false);
                        r->setToolTip(laterTip());
                    }
                    group->addButton(r, i);
                    buttons->addWidget(r);
                }
                if (o.label.isEmpty()) buttons->setContentsMargins(0, 0, 0, 0);
                editor = row = holder;
            } else {
                // A label, the field, and what follows it.
                row = new QWidget;
                auto* h = new QHBoxLayout(row);
                h->setContentsMargins(0, 0, 0, 0);
                if (!o.label.isEmpty() && o.type != QLatin1String("button")) h->addWidget(new QLabel(o.label, row));
                if (o.type == QLatin1String("int")) {
                    auto* spin = new QSpinBox(row);
                    spin->setRange(o.min, o.max);
                    spin->setValue(current.toInt());
                    if (!o.specialText.isEmpty()) spin->setSpecialValueText(o.specialText);
                    editor = spin;
                    if (!o.resetButton.isEmpty()) {
                        // "To return this option to its default value, click the rs (recommended
                        // sample) button" (p.1147).
                        auto* reset = new QToolButton(row);
                        reset->setText(o.resetButton);
                        reset->setObjectName(o.key + QStringLiteral("Reset"));
                        const int fallback = o.defaultValue.toInt();
                        QObject::connect(reset, &QToolButton::clicked, spin, [spin, fallback] { spin->setValue(fallback); });
                    }
                } else if (o.type == QLatin1String("number")) {
                    // A value with decimals (the Safe Colors limits, "0.00", "108.45", p.723).
                    auto* spin = new QDoubleSpinBox(row);
                    spin->setDecimals(o.decimals);
                    spin->setRange(o.minNumber, o.maxNumber);
                    spin->setValue(current.toDouble());
                    editor = spin;
                } else if (o.type == QLatin1String("choice")) {
                    auto* menu = new QComboBox(row);
                    for (const SettingChoice& c : o.choices) {
                        menu->addItem(c.label, c.value);
                        if (c.later) {
                            if (auto* m = qobject_cast<QStandardItemModel*>(menu->model())) {
                                m->item(menu->count() - 1)->setEnabled(false);
                            }
                        }
                    }
                    menu->setCurrentIndex(std::max(menu->findData(current.toString()), 0));
                    editor = menu;
                } else if (o.type == QLatin1String("drive")) {
                    // The media drives; the one with the most space in
                    // bold, and chosen when the setting names none (p.135).
                    auto* menu = new QComboBox(row);
                    const QList<media::MediaDrive> drives = media::mediaDrives();
                    const int best = media::mostSpace(drives);
                    for (int i = 0; i < drives.size(); ++i) {
                        menu->addItem(drives.at(i).name, drives.at(i).root);
                        if (i == best) {
                            QFont bold = menu->font();
                            bold.setBold(true);
                            menu->setItemData(i, bold, Qt::FontRole);
                        }
                    }
                    const QString chosen = media::chooseDrive(current.toString());
                    menu->setCurrentIndex(std::max(menu->findData(chosen), 0));
                    editor = menu;
                } else if (o.type == QLatin1String("color")) {
                    // A swatch button: click for the colour picker; its menu also clears it when the
                    // option may be empty (no colour). The value is the "color" property, "#rrggbb".
                    auto* button = new QToolButton(row);
                    button->setPopupMode(QToolButton::MenuButtonPopup);
                    auto show = [button](const QString& hex) {
                        button->setProperty("color", hex);
                        QPixmap swatch(28, 14);
                        swatch.fill(hex.isEmpty() ? QColor(Qt::transparent) : QColor(hex));
                        button->setIcon(QIcon(swatch));
                        button->setText(hex.isEmpty() ? tr("None") : hex);
                        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                    };
                    show(current.toString());
                    auto pick = [button, show] {
                        const QColor start(button->property("color").toString());
                        const QColor c = QColorDialog::getColor(start.isValid() ? start : QColor(Qt::gray), button);
                        if (c.isValid()) show(c.name());
                    };
                    QObject::connect(button, &QToolButton::clicked, button, pick);
                    auto* menu = new QMenu(button);
                    menu->addAction(tr("Choose Color..."), button, pick);
                    if (o.defaultValue.toString().isEmpty()) menu->addAction(tr("No Color"), button, [show] { show(QString()); });
                    button->setMenu(menu);
                    editor = button;
                } else if (o.type == QLatin1String("font")) {
                    // The system's font families, after "Default" (empty: the application's font).
                    auto* menu = new QComboBox(row);
                    menu->addItem(tr("Default"), QString());
                    for (const QString& family : QFontDatabase::families()) menu->addItem(family, family);
                    menu->setCurrentIndex(std::max(menu->findData(current.toString()), 0));
                    editor = menu;
                } else if (o.type == QLatin1String("info")) {
                    editor = new QLabel(row);
                } else if (o.type == QLatin1String("button")) {
                    // A button opening a window of its own (Transcript's Manage Speakers, p.445): buttonClicked(key).
                    auto* button = new QPushButton(o.label, row);
                    connect(button, &QPushButton::clicked, this, [this, key = o.key] { emit buttonClicked(key); });
                    editor = button;
                } else {
                    auto* line = new QLineEdit(current.toString(), row);
                    if (o.type == QLatin1String("timecode")) {
                        line->setValidator(new QRegularExpressionValidator(
                            QRegularExpression(QStringLiteral("\\d{2}[:;.]\\d{2}[:;.]\\d{2}[:;.]\\d{2}")), line));
                    }
                    editor = line;
                }
                h->addWidget(editor);
                if (auto* reset = row->findChild<QToolButton*>(o.key + QStringLiteral("Reset"))) h->addWidget(reset);
                if (!o.suffix.isEmpty()) h->addWidget(new QLabel(o.suffix, row));
                h->addStretch(1);
            }
            editor->setObjectName(o.key);
            if (o.later) {
                row->setEnabled(false);
                row->setToolTip(laterTip());
            }
            editors_.insert(o.key, editor);
            v->addWidget(row);
        }
        v->addStretch(1);
        return w;
    };

    auto* layout = new QVBoxLayout(this);
    if (kind.tabs.size() > 1) {
        tabs_ = new QTabWidget(this);
        tabs_->setObjectName(QStringLiteral("settingsTabs"));
        for (const SettingTab& t : kind.tabs) tabs_->addTab(page(t), t.name);
        layout->addWidget(tabs_);
    } else if (!kind.tabs.isEmpty()) {
        layout->addWidget(page(kind.tabs.constFirst()));
    }
    // A graph redraws as the dialog's values change.
    for (QWidget* w : std::as_const(editors_)) {
        auto* graph = qobject_cast<SafeColorGraph*>(w);
        if (!graph) continue;
        for (QWidget* e : std::as_const(editors_)) {
            if (auto* number = qobject_cast<QDoubleSpinBox*>(e)) connect(number, &QDoubleSpinBox::valueChanged, graph, qOverload<>(&QWidget::update));
            if (auto* menu = qobject_cast<QComboBox*>(e)) connect(menu, &QComboBox::currentIndexChanged, graph, qOverload<>(&QWidget::update));
        }
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::setInfo(const QString& key, const QString& text) {
    if (auto* label = qobject_cast<QLabel*>(editors_.value(key))) label->setText(text);
}

QWidget* SettingsDialog::editor(const QString& key) const {
    return editors_.value(key);
}

// Reads each control back by its type. Timecodes are checked against the project's timecode format
// (drop-frame when written with ';', as model::parseTimecode reads them) to tell whether they parse;
// without a project, against 30 fps.
QJsonObject SettingsDialog::values() const {
    QJsonObject out = values_;
    for (const SettingTab& t : kind_.tabs) {
        for (const SettingOption& o : t.options) {
            if (o.later || o.type == QLatin1String("info") || o.type == QLatin1String("safeColorGraph") ||
                o.type == QLatin1String("button")) {
                continue;
            }
            QWidget* e = editors_.value(o.key);
            if (auto* box = qobject_cast<QCheckBox*>(e)) {
                out.insert(o.key, box->isChecked());
            } else if (auto* spin = qobject_cast<QSpinBox*>(e)) {
                out.insert(o.key, spin->value());
            } else if (auto* number = qobject_cast<QDoubleSpinBox*>(e)) {
                out.insert(o.key, number->value());
            } else if (auto* menu = qobject_cast<QComboBox*>(e)) {
                out.insert(o.key, menu->currentData().toString());
            } else if (o.type == QLatin1String("color") && e) {
                out.insert(o.key, e->property("color").toString());
            } else if (auto* line = qobject_cast<QLineEdit*>(e)) {
                QString text = line->text().trimmed();
                if (o.type == QLatin1String("timecode")) {
                    TimecodeFormat format = TimecodeFormat::forRate(editRate_, text.contains(QLatin1Char(';')));
                    if (!format.isValid()) format = TimecodeFormat::forRate(editRate_, false);
                    if (!Timecode::parse(text.toStdString(), format)) text = o.defaultValue.toString();
                }
                out.insert(o.key, text);
            } else if (e) {
                if (auto* group = e->findChild<QButtonGroup*>(o.key + QStringLiteral("Group"))) {
                    const int id = group->checkedId();
                    if (id >= 0) out.insert(o.key, o.choices.at(id).value);
                }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- SettingsList

SettingsList::SettingsList(Settings& settings, SettingsScope scope, QWidget* parent)
    : QTreeWidget(parent), settings_(settings), scope_(scope) {
    setObjectName(scope == SettingsScope::Project ? QStringLiteral("projectSettingsList")
                  : scope == SettingsScope::User  ? QStringLiteral("userSettingsList")
                                                  : QStringLiteral("siteSettingsList"));
    setColumnCount(2);
    setHeaderLabels({tr("Setting"), tr("Name")});
    header()->setStretchLastSection(true);
    setColumnWidth(kSettingColumn, 180);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    setItemDelegate(new NameDelegate(this));
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setRootIsDecorated(true);

    // Double-click: the Name column edits the name, any other column opens the dialog.
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        const QVariant entry = item->data(kSettingColumn, kEntryRole);
        if (!entry.isValid()) return;
        if (column == kNameColumn) {
            editItem(item, kNameColumn);
        } else {
            openEntry(entry.toInt());
        }
    });
    // An edited name or a checked active box. The change is applied after the signal returns because
    // committing it rebuilds the list and deletes `item`.
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (filling_) return;
        const QVariant entry = item->data(kSettingColumn, kEntryRole);
        if (!entry.isValid()) return;
        // After the signal: the list is rebuilt, and with it this item.
        const int index = entry.toInt();
        if (column == kNameColumn) {
            const QString name = item->text(kNameColumn);
            QMetaObject::invokeMethod(this, [this, index, name] { rename(index, name); }, Qt::QueuedConnection);
        } else if (column == kSettingColumn && item->checkState(kSettingColumn) == Qt::Checked) {
            QMetaObject::invokeMethod(this, [this, index] { setActive(index); }, Qt::QueuedConnection);
        } else if (column == kSettingColumn) {
            // The active version cannot be unchecked: choose another.
            QMetaObject::invokeMethod(this, &SettingsList::refresh, Qt::QueuedConnection);
        }
    });
    connect(&settings_, &Settings::changed, this, &SettingsList::refresh);
    refresh();
}

// A kind with one version is a single row; one with several is a group row with a checkable row
// per version. filling_ keeps the rebuild from being taken as user edits.
void SettingsList::refresh() {
    filling_ = true;
    QSet<QString> expanded;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        if (topLevelItem(i)->isExpanded()) expanded << topLevelItem(i)->text(kSettingColumn);
    }
    const QList<int> selected = selectedEntries();
    clear();
    const SettingsFile& f = file();
    auto entryItem = [&](int index, QTreeWidgetItem* item) {
        const SettingEntry& e = f.entries.at(index);
        item->setText(kSettingColumn, e.kind);
        item->setText(kNameColumn, e.name);
        item->setData(kSettingColumn, kEntryRole, index);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
    };
    for (const QString& kind : f.kinds()) {
        const QList<int> versions = f.versions(kind);
        if (versions.size() == 1) {
            entryItem(versions.constFirst(), new QTreeWidgetItem(this));
            continue;
        }
        // Several versions: under the setting's name, with check boxes for
        // the active one (picture on p.1136).
        auto* group = new QTreeWidgetItem(this);
        group->setText(kSettingColumn, kind);
        group->setFlags(Qt::ItemIsEnabled);
        for (int i : versions) {
            auto* item = new QTreeWidgetItem(group);
            entryItem(i, item);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(kSettingColumn, f.entries.at(i).active ? Qt::Checked : Qt::Unchecked);
        }
        group->setExpanded(expanded.contains(kind));
    }
    selectEntries(selected);
    filling_ = false;
}

QList<int> SettingsList::selectedEntries() const {
    QList<int> out;
    for (QTreeWidgetItem* item : selectedItems()) {
        const QVariant v = item->data(kSettingColumn, kEntryRole);
        if (v.isValid()) out << v.toInt();
    }
    std::sort(out.begin(), out.end());
    return out;
}

QTreeWidgetItem* SettingsList::itemFor(int entry) const {
    for (QTreeWidgetItemIterator it(const_cast<SettingsList*>(this)); *it; ++it) {
        const QVariant v = (*it)->data(kSettingColumn, kEntryRole);
        if (v.isValid() && v.toInt() == entry) return *it;
    }
    return nullptr;
}

void SettingsList::selectEntries(const QList<int>& entries) {
    clearSelection();
    for (int e : entries) {
        if (QTreeWidgetItem* item = itemFor(e)) {
            item->setSelected(true);
            if (item->parent()) item->parent()->setExpanded(true);
            setCurrentItem(item, kSettingColumn, QItemSelectionModel::NoUpdate);
        }
    }
}

void SettingsList::commit() {
    settings_.commit(scope_);
    emit edited();
}

// Each copy is inserted after the last version of its kind, so the indices of copies made so far are
// shifted past each new insertion.
void SettingsList::duplicateSelection() {
    QList<int> entries = selectedEntries();
    if (entries.isEmpty()) return;
    // Later entries first, so earlier indices stay put.
    std::sort(entries.begin(), entries.end(), std::greater<>());
    QList<int> copies;
    for (int e : entries) {
        const int copy = file().duplicate(e);
        for (int& c : copies) {
            if (c >= copy) ++c;
        }
        copies << copy;
    }
    commit();
    selectEntries(copies);
}

void SettingsList::deleteSelection() {
    const QList<int> entries = selectedEntries();
    if (entries.isEmpty()) return;
    file().remove(entries);
    // Settings this scope needs come back with their defaults.
    if (scope_ != SettingsScope::Site) file().ensure(SettingsSchema::builtIn().kindNames(scope_));
    commit();
}

// Restoring empties the entry's values, so every option reads its default.
void SettingsList::restoreSelection(Restore how) {
    QList<int> entries = selectedEntries();
    if (entries.isEmpty()) return;
    if (how == Restore::Ask) {
        QMessageBox box(QMessageBox::Question, tr("Restore to Default"),
                        tr("Restore the selected settings to their default values?"), QMessageBox::NoButton, this);
        QPushButton* copy = box.addButton(tr("Copy && Restore"), QMessageBox::AcceptRole);
        QPushButton* restore = box.addButton(tr("Restore"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == copy) {
            how = Restore::CopyAndRestore;
        } else if (box.clickedButton() == restore) {
            how = Restore::Restore;
        } else {
            return;
        }
    }
    std::sort(entries.begin(), entries.end(), std::greater<>());
    QList<int> restored;
    for (int e : entries) {
        if (how == Restore::CopyAndRestore) {
            // The copy keeps the current values; it goes after e.
            const int copy = file().duplicate(e);
            for (int& r : restored) {
                if (r >= copy) ++r;
            }
        }
        file().entries[e].values = {};
        restored << e;
    }
    commit();
    selectEntries(restored);
}

void SettingsList::setActive(int entry) {
    if (entry < 0 || entry >= file().entries.size()) return;
    file().setActive(entry);
    commit();
}

void SettingsList::rename(int entry, const QString& name) {
    if (entry < 0 || entry >= file().entries.size() || file().entries.at(entry).name == name.trimmed()) return;
    file().entries[entry].name = name.trimmed();
    commit();
}

// Site settings take any kind and always add; Project and User settings skip kinds of the other scope.
int SettingsList::copyIn(const QList<SettingEntry>& entries, bool replace) {
    const QStringList kinds = SettingsSchema::builtIn().kindNames(scope_);
    int copied = 0;
    for (const SettingEntry& e : entries) {
        if (scope_ != SettingsScope::Site && !kinds.contains(e.kind)) continue;
        file().add(e, replace && scope_ != SettingsScope::Site);
        ++copied;
    }
    if (copied) commit();
    return copied;
}

// The dialog edits a copy of the entry; the file changes only when OK is clicked with different values.
bool SettingsList::openEntry(int entry) {
    if (entry < 0 || entry >= file().entries.size()) return false;
    const SettingEntry e = file().entries.at(entry);
    const SettingKind* kind = SettingsSchema::builtIn().find(e.kind);
    if (!kind) return false;
    if (!kind->opens.isEmpty()) {
        // Its version becomes the active one, then the tool opens (p.392).
        if (!e.active) setActive(entry);
        emit toolRequested(kind->opens);
        return false;
    }
    SettingsDialog dialog(*kind, e, this);
    dialog.setInfo(QStringLiteral("projectFormat"), projectFormat);
    if (projectEditRate) dialog.setEditRate(*projectEditRate);
    // A setting's button opens its tool over the dialog (MainWindow, through toolRequested).
    connect(&dialog, &SettingsDialog::buttonClicked, this, &SettingsList::toolRequested);
    if (dialogHook) dialogHook(dialog);
    if (dialog.exec() != QDialog::Accepted) return false;
    const QJsonObject values = dialog.values();
    if (values == e.values) return false;
    file().entries[entry].values = values;
    commit();
    return true;
}

QMenu* SettingsList::contextMenu() {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("settingsContextMenu"));
    const bool any = !selectedEntries().isEmpty();
    menu->addAction(tr("Duplicate"), this, &SettingsList::duplicateSelection)->setEnabled(any);
    menu->addAction(tr("Delete"), this, &SettingsList::deleteSelection)->setEnabled(any);
    menu->addSeparator();
    menu->addAction(tr("Restore to Default"), this, [this] { restoreSelection(); })->setEnabled(any);
    return menu;
}

// Right-clicking an unselected row selects it alone first, so the menu acts on that row.
void SettingsList::contextMenuEvent(QContextMenuEvent* event) {
    if (QTreeWidgetItem* item = itemAt(event->pos()); item && !item->isSelected()) {
        clearSelection();
        item->setSelected(true);
    }
    QMenu* menu = contextMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

void SettingsList::keyPressEvent(QKeyEvent* event) {
    // Enter opens the setting; Delete deletes (p.1138, p.1140).
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && state() != EditingState) {
        const QList<int> entries = selectedEntries();
        if (!entries.isEmpty()) openEntry(entries.constFirst());
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && state() != EditingState) {
        deleteSelection();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

QStringList SettingsList::mimeTypes() const {
    return {mimeType()};
}

// Drag data for the dragged version rows (group rows are skipped); nullptr if there are none.
QMimeData* SettingsList::mimeData(const QList<QTreeWidgetItem*>& items) const {
    QList<SettingEntry> entries;
    for (QTreeWidgetItem* item : items) {
        const QVariant v = item->data(kSettingColumn, kEntryRole);
        if (v.isValid()) entries << file().entries.at(v.toInt());
    }
    if (entries.isEmpty()) return nullptr;
    auto* mime = new QMimeData;
    mime->setData(mimeType(), QJsonDocument(QJsonObject{{QStringLiteral("settings"), entriesJson(entries)}}).toJson());
    return mime;
}

bool SettingsList::accepts(const QMimeData* mime) const {
    return mime && mime->hasFormat(mimeType());
}

// Settings are copied between lists only; a drag within the same list is refused.
void SettingsList::dragEnterEvent(QDragEnterEvent* event) {
    if (event->source() != this && accepts(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void SettingsList::dragMoveEvent(QDragMoveEvent* event) {
    if (event->source() != this && accepts(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void SettingsList::dropEvent(QDropEvent* event) {
    if (event->source() == this || !accepts(event->mimeData())) {
        event->ignore();
        return;
    }
    const QList<SettingEntry> entries = entriesFrom(event->mimeData());
    event->setDropAction(Qt::CopyAction);
    event->accept();
    bool replace = false;
    if (scope_ != SettingsScope::Site) {
        // p.1141: Add, or Replace the current version.
        QMessageBox box(QMessageBox::Question, tr("Settings"), tr("Add the settings, or replace the current ones?"),
                        QMessageBox::NoButton, this);
        QPushButton* add = box.addButton(tr("Add"), QMessageBox::AcceptRole);
        QPushButton* rep = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != add && box.clickedButton() != rep) return;
        replace = box.clickedButton() == rep;
    }
    copyIn(entries, replace);
}

// ---------------------------------------------------------------- SettingsWindow

// Four tabs in Tab order: Format (a form filled by setProject), Project, User (with the User Profile
// menu above its list) and Site. Opens on the User tab.
SettingsWindow::SettingsWindow(Settings& settings, QWidget* parent) : QWidget(parent, Qt::Tool), settings_(settings) {
    setObjectName(QStringLiteral("settingsWindow"));
    setWindowTitle(tr("Settings"));
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("settingsWindowTabs"));

    // Format: the project's format, from New Project (p.1146).
    auto* format = new QWidget;
    format_ = new QFormLayout(format);
    // Fields fill the row on every platform (macOS's style keeps them at their size hint otherwise), so that menus sized
    // from a minimum (Project Type) still show their text.
    format_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    tabs_->addTab(format, tr("Format"));

    project_ = new SettingsList(settings, SettingsScope::Project);
    tabs_->addTab(project_, tr("Project"));

    auto* user = new QWidget;
    auto* uv = new QVBoxLayout(user);
    uv->setContentsMargins(0, 4, 0, 0);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("User Profile:"), user));
    profiles_ = new QComboBox(user);
    profiles_->setObjectName(QStringLiteral("userProfileMenu"));
    profiles_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    row->addWidget(profiles_);
    row->addStretch(1);
    uv->addLayout(row);
    user_ = new SettingsList(settings, SettingsScope::User);
    uv->addWidget(user_, 1);
    tabs_->addTab(user, tr("User"));

    site_ = new SettingsList(settings, SettingsScope::Site);
    tabs_->addTab(site_, tr("Site"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(tabs_);
    resize(380, 520);
    showTab(Tab::User);

    // The User Profile menu: a profile name switches user; the items with data are its commands.
    // Update User Profiles adds default versions of settings the user's file lacks.
    connect(profiles_, &QComboBox::activated, this, [this](int index) {
        if (filling_) return;
        const QString action = profiles_->itemData(index).toString();
        if (action == QLatin1String("create")) {
            CreateUserProfileDialog dialog(this);
            if (dialog.exec() == QDialog::Accepted) createUser(dialog.profileName());
        } else if (action == QLatin1String("update")) {
            settings_.file(SettingsScope::User).ensure(SettingsSchema::builtIn().kindNames(SettingsScope::User));
            // "If the setting does not appear, click the User Profile Selection menu and select Update User Profiles"
            // (p.884): shipped versions such as Link To Effects Mixdown come back too.
            SettingsSchema::builtIn().addShippedVersions(settings_.file(SettingsScope::User), SettingsScope::User);
            settings_.commit(SettingsScope::User);
        } else if (action == QLatin1String("reveal")) {
            const QString folder = QDir(settings_.usersRoot()).filePath(settings_.user());
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        } else {
            selectUser(profiles_->itemText(index));
        }
        fillProfiles();
    });
    connect(&settings_, &Settings::changed, this, &SettingsWindow::fillProfiles);
    for (SettingsList* l : {project_, user_, site_}) {
        connect(l, &SettingsList::toolRequested, this, &SettingsWindow::toolRequested);
    }
    fillProfiles();
    setProject(std::nullopt);
}

// The profiles in the users folder (plus the current user if it has no folder yet), a separator and
// the commands. Create and Reveal need a users folder, so they are disabled while settings are kept
// in memory only.
void SettingsWindow::fillProfiles() {
    filling_ = true;
    profiles_->clear();
    QStringList users;
    if (!settings_.usersRoot().isEmpty()) users = model::UserProfiles(settings_.usersRoot()).list();
    if (!settings_.user().isEmpty() && !users.contains(settings_.user())) users << settings_.user();
    for (const QString& u : users) profiles_->addItem(u);
    profiles_->insertSeparator(profiles_->count());
    // The User Profile Selection menu (p.1143-1145).
    profiles_->addItem(tr("Create User Profile..."), QStringLiteral("create"));
    profiles_->addItem(tr("Update User Profiles"), QStringLiteral("update"));
    profiles_->addItem(tr("Reveal User Profile"), QStringLiteral("reveal"));
    const bool saved = !settings_.usersRoot().isEmpty();
    for (const char* action : {"create", "reveal"}) {
        if (auto* m = qobject_cast<QStandardItemModel*>(profiles_->model())) {
            m->item(profiles_->findData(QString::fromLatin1(action)))->setEnabled(saved);
        }
    }
    profiles_->setCurrentIndex(std::max(static_cast<int>(users.indexOf(settings_.user())), 0));
    filling_ = false;
}

bool SettingsWindow::selectUser(const QString& name) {
    if (name.isEmpty() || name == settings_.user()) return false;
    // The previous user's settings are saved as they change; load the new
    // user's (p.1138).
    settings_.setUser(settings_.usersRoot(), name);
    return true;
}

bool SettingsWindow::createUser(const QString& name) {
    if (settings_.usersRoot().isEmpty()) return false;
    QString error;
    if (!model::UserProfiles(settings_.usersRoot()).create(name.trimmed(), &error)) {
        QMessageBox::warning(this, tr("Create User Profile"), error);
        return false;
    }
    // "The new user profile appears selected in the menu" (p.1143).
    settings_.setUser(settings_.usersRoot(), name.trimmed());
    return true;
}

void SettingsWindow::setProject(const std::optional<model::ProjectEntry>& project) {
    entry_ = project;
    while (format_->rowCount() > 0) format_->removeRow(0);
    tabs_->setTabEnabled(static_cast<int>(Tab::Project), project.has_value());
    QString formatText;
    if (project) {
        const model::ProjectSettings& s = project->settings;
        formatText = QString::fromStdString(s.format.label());
        auto add = [this](const QString& label, const QString& value, const char* name) {
            auto* l = new QLabel(value);
            l->setObjectName(QString::fromLatin1(name));
            format_->addRow(label, l);
        };
        add(tr("Project Name:"), s.name, "formatProjectName");
        // "Select File > Settings, and click the Format tab to change the format of the project to another format that
        // shares the same frame rate ... You can select only projects with the same edit rate" (p.1287-1288; 8.7c1b).
        auto* type = new QComboBox;
        type->setObjectName(QStringLiteral("formatProjectType"));
        QList<ProjectFormat> sameRate{s.format};
        for (const ProjectFormat& f : projectFormatPresets()) {
            if (f.editRate == s.format.editRate && !sameRate.contains(f)) sameRate << f;
        }
        for (const ProjectFormat& f : sameRate) type->addItem(QString::fromStdString(f.label()));
        type->setEnabled(sameRate.size() > 1);
        // Not sized from its longest format name: the arrow's padding differs by platform style, and the tab's width
        // (and the cross-platform screenshot) would follow it.
        type->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        type->setMinimumContentsLength(1);
        connect(type, &QComboBox::activated, this, [this, sameRate](int index) {
            if (index > 0 && index < sameRate.size()) emit projectFormatChanged(sameRate.at(index));
        });
        format_->addRow(tr("Project Type:"), type);
        add(tr("Aspect Ratio:"), QStringLiteral("%1:%2").arg(s.aspect.num()).arg(s.aspect.den()), "formatAspect");
        add(tr("Raster Dimension:"), QStringLiteral("%1 x %2").arg(s.format.width).arg(s.format.height),
            "formatRaster");
        // Proxy (p.526-527, p.549-550; 8.5e2): "play your sequence at 1/4 or 1/16 of the current project resolution",
        // only in high-resolution projects; a mode whose raster would be under 256 x 120 is not offered. The item texts
        // are ours.
        auto* proxy = new QComboBox;
        proxy->setObjectName(QStringLiteral("formatProxy"));
        for (const int mode : model::proxyModes(s.format)) {
            proxy->addItem(mode == 0 ? tr("Off") : QStringLiteral("1/%1").arg(mode), mode);
        }
        proxy->setCurrentIndex(std::max(0, proxy->findData(s.proxy)));
        proxy->setEnabled(proxy->count() > 1);
        connect(proxy, &QComboBox::activated, this, [this, proxy](int index) { emit proxyChanged(proxy->itemData(index).toInt()); });
        format_->addRow(tr("Proxy:"), proxy);
        // ACES projects (p.559): "You will notice the ACES version is v1.1. ... From the Output Color Space, select the
        // color space you want for your exported sequence."
        if (model::isAcesColorSpace(s.colorSpace)) {
            add(tr("ACES Version:"), model::acesVersion(), "formatAcesVersion");
            auto* output = new QComboBox;
            output->setObjectName(QStringLiteral("formatOutputColorSpace"));
            output->addItems(model::acesOutputColorSpaces());
            output->setCurrentText(s.outputColorSpace);
            connect(output, &QComboBox::activated, this, [this, output](int index) { emit outputColorSpaceChanged(output->itemText(index)); });
            format_->addRow(tr("Output Color Space:"), output);
        }
        // "Click the Mask Margins button. The Target Settings dialog displays" (p.551).
        auto* mask = new QPushButton(tr("Mask Margins"));
        mask->setObjectName(QStringLiteral("maskMarginsButton"));
        connect(mask, &QPushButton::clicked, this, &SettingsWindow::openMaskMargins);
        format_->addRow(QString(), mask);
        add(tr("Color Space:"), s.colorSpace, "formatColorSpace");
        add(tr("Color Depth:"), s.colorDepth, "formatColorDepth");
        add(tr("Stereoscopic:"), s.stereoscopic, "formatStereoscopic");
    } else {
        format_->addRow(new QLabel(tr("No project is open.")));
    }
    for (SettingsList* l : {project_, user_, site_}) {
        l->projectFormat = formatText;
        l->projectEditRate = project ? std::optional<Rational>(project->settings.format.editRate) : std::nullopt;
    }
}

void SettingsWindow::openMaskMargins() {
    if (!entry_) return;
    const Rational aspect = entry_->settings.aspect;
    const double ratio = aspect.den() > 0 ? static_cast<double>(aspect.num()) / static_cast<double>(aspect.den()) : 16.0 / 9.0;
    TargetSettingsDialog dialog(entry_->settings.mask, ratio, this);
    connect(&dialog, &TargetSettingsDialog::applied, this, &SettingsWindow::maskMarginsChanged);
    const bool ok = maskHook_ ? maskHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (ok) emit maskMarginsChanged(dialog.margins());
}

void SettingsWindow::showTab(Tab tab) {
    tabs_->setCurrentIndex(static_cast<int>(tab));
}

SettingsWindow::Tab SettingsWindow::currentTab() const {
    return static_cast<Tab>(tabs_->currentIndex());
}

SettingsList* SettingsWindow::list(SettingsScope scope) const {
    switch (scope) {
    case SettingsScope::Project: return project_;
    case SettingsScope::User: return user_;
    case SettingsScope::Site: return site_;
    }
    return nullptr;
}

SettingsList* SettingsWindow::currentList() const {
    switch (currentTab()) {
    case Tab::Project: return project_;
    case Tab::User: return user_;
    case Tab::Site: return site_;
    case Tab::Format: break;
    }
    return nullptr;
}

void SettingsWindow::refresh() {
    for (SettingsList* l : {project_, user_, site_}) l->refresh();
    fillProfiles();
}

// ---------------------------------------------------------------- SiteSettingsWindow

SiteSettingsWindow::SiteSettingsWindow(Settings& settings, QWidget* parent) : QWidget(parent, Qt::Tool) {
    setObjectName(QStringLiteral("siteSettingsWindow"));
    setWindowTitle(tr("Site Settings"));
    list_ = new SettingsList(settings, SettingsScope::Site, this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(list_);
    resize(360, 360);
}

// ---------------------------------------------------------------- CreateUserProfileDialog

CreateUserProfileDialog::CreateUserProfileDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("createUserProfileDialog"));
    setWindowTitle(tr("Create User Profile"));
    auto* form = new QFormLayout;
    // The profile's default settings (p.1143); this product has one set.
    auto* defaults = new QComboBox(this);
    defaults->setObjectName(QStringLiteral("profileDefaults"));
    defaults->addItem(QString::fromUtf8(branding::kProductName));
    form->addRow(tr("Default settings:"), defaults);
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("profileName"));
    form->addRow(tr("Profile Name:"), name_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(name_, &QLineEdit::textChanged, this, [buttons](const QString& text) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString CreateUserProfileDialog::profileName() const {
    return name_->text().trimmed();
}

} // namespace lumenis::ui
