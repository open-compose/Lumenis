#include "lumenis/ui/effect_palette.h"

#include "lumenis/media/audio_fx.h"

#include <QContextMenuEvent>
#include <QDrag>
#include <QFile>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSplitter>
#include <QTabBar>
#include <QVBoxLayout>

// The Effect Palette (effect_palette.h): the built-in effect catalog read from resources/effects.json,
// the drag data that carries an effect to the Timeline (timeline.cpp takes the drop), the effect icon,
// and the palette widget. Applying an effect to a segment or transition is the Timeline's and the
// sequence model's (model::Effect in src/model/sequence.*); its parameters are effect_editor.*.

namespace lumenis::ui {

namespace {

// The category entry that lists every category's effects.
const QString kAll = QStringLiteral("All");

// Our own effect tile colours (the guide's icons are not copied).
constexpr QRgb kTile = 0xff8f5fc8;
constexpr QRgb kTileDark = 0xff4b2f72;
constexpr QRgb kRealTime = 0xff3cc85a;

} // namespace

// ---------------------------------------------------------------- EffectCatalog

// effects.json holds "tabs" (name and kind) and "categories", each with its "effects". An effect's
// "realTime" is true when left out.
const EffectCatalog& EffectCatalog::builtin() {
    static const EffectCatalog catalog = [] {
        EffectCatalog c;
        QFile f(QStringLiteral(":/lumenis/ui/effects.json"));
        if (!f.open(QIODevice::ReadOnly)) return c;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue& v : root.value(QStringLiteral("tabs")).toArray()) {
            const QJsonObject o = v.toObject();
            c.tabs_ << Tab{o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("kind")).toString()};
        }
        for (const QJsonValue& v : root.value(QStringLiteral("categories")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString category = o.value(QStringLiteral("name")).toString();
            c.categories_ << category;
            for (const QJsonValue& ev : o.value(QStringLiteral("effects")).toArray()) {
                const QJsonObject e = ev.toObject();
                EffectInfo info;
                info.category = category;
                info.name = e.value(QStringLiteral("name")).toString();
                info.page = e.value(QStringLiteral("page")).toInt();
                info.transition = e.value(QStringLiteral("transition")).toBool();
                info.segment = e.value(QStringLiteral("segment")).toBool();
                info.realTime = e.value(QStringLiteral("realTime")).toBool(true);
                for (const QJsonValue& p : e.value(QStringLiteral("parameters")).toArray()) {
                    info.parameters << p.toString();
                }
                c.effects_ << info;
            }
        }
        return c;
    }();
    return catalog;
}

QStringList EffectCatalog::categories() const {
    return categories_;
}

QList<EffectInfo> EffectCatalog::effectsIn(const QString& category) const {
    QList<EffectInfo> out;
    for (const EffectInfo& e : effects_) {
        if (e.category == category) out << e;
    }
    return out;
}

const EffectInfo* EffectCatalog::find(const QString& category, const QString& name) const {
    for (const EffectInfo& e : effects_) {
        if (e.category == category && e.name == name) return &e;
    }
    return nullptr;
}

// The Audio Track tab's effects (p.791-792): media::audioEffects() as palette entries, by category.
QList<EffectInfo> audioTrackEffects() {
    QList<EffectInfo> out;
    for (const media::AudioEffectInfo& a : media::audioEffects()) {
        EffectInfo e;
        e.category = a.category;
        e.name = a.name;
        e.audioEffect = a.id;
        out << e;
    }
    return out;
}

QMimeData* effectMime(const EffectInfo& effect) {
    auto* mime = new QMimeData;
    QJsonObject o{{QStringLiteral("category"), effect.category}, {QStringLiteral("name"), effect.name}};
    if (!effect.audioEffect.isEmpty()) o.insert(QStringLiteral("audioEffect"), effect.audioEffect);
    mime->setData(QString::fromLatin1(kEffectMimeType), QJsonDocument(o).toJson(QJsonDocument::Compact));
    mime->setText(effect.name);
    return mime;
}

std::optional<EffectInfo> effectFromMime(const QMimeData* mime) {
    if (!mime || !mime->hasFormat(QString::fromLatin1(kEffectMimeType))) return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(mime->data(QString::fromLatin1(kEffectMimeType))).object();
    if (const QString id = o.value(QStringLiteral("audioEffect")).toString(); !id.isEmpty()) {
        for (const EffectInfo& a : audioTrackEffects()) {
            if (a.audioEffect == id) return a;
        }
        return std::nullopt;
    }
    const EffectInfo* e = EffectCatalog::builtin().find(o.value(QStringLiteral("category")).toString(),
                                                        o.value(QStringLiteral("name")).toString());
    if (!e) return std::nullopt;
    return *e;
}

// A 26x18 pixmap: the optional green dot on the left, then a purple tile.
QIcon effectIcon(bool transition, bool realTimeDot) {
    QPixmap pm(26, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (realTimeDot) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(kRealTime));
        p.drawEllipse(QRectF(1, 7, 4, 4));
    }
    const QRectF tile(8, 2, 17, 14);
    p.setPen(QPen(QColor(kTileDark), 1));
    p.setBrush(QColor(kTile));
    p.drawRoundedRect(tile, 2, 2);
    p.setBrush(QColor(kTileDark));
    if (transition) {
        // Two pictures meeting: a diagonal split.
        p.drawPolygon(QPolygonF{tile.topRight(), tile.bottomRight(), tile.bottomLeft() + QPointF(5, 0)});
    } else {
        // A picture within the frame.
        p.drawRect(tile.adjusted(4, 3, -4, -3));
    }
    return QIcon(pm);
}

// ---------------------------------------------------------------- EffectList

// Each item's text is the effect's name and its Qt::UserRole its category, which find the effect.
void EffectList::startDrag(Qt::DropActions) {
    QListWidgetItem* item = currentItem();
    if (!item) return;
    const EffectInfo* e = EffectCatalog::builtin().find(item->data(Qt::UserRole).toString(), item->text());
    if (!e) return;
    auto* drag = new QDrag(this);
    drag->setMimeData(effectMime(*e));
    drag->setPixmap(item->icon().pixmap(26, 18));
    drag->exec(Qt::CopyAction);
}

// ---------------------------------------------------------------- EffectPalette

// The tab bar, Quick Find, then the categories and effects side by side in a splitter.
EffectPalette::EffectPalette(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("effectPalette"));
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("effectTabs"));
    tabs_->setDrawBase(false);
    for (const EffectCatalog::Tab& t : EffectCatalog::builtin().tabs()) tabs_->addTab(t.name);
    find_ = new QLineEdit(this);
    find_->setObjectName(QStringLiteral("effectQuickFind"));
    find_->setPlaceholderText(tr("Quick Find"));
    find_->setClearButtonEnabled(true);
    find_->setMaximumWidth(190);

    categories_ = new QListWidget(this);
    categories_->setObjectName(QStringLiteral("effectCategories"));
    effects_ = new EffectList(this);
    effects_->setObjectName(QStringLiteral("effectList"));
    effects_->setDragEnabled(true);
    effects_->setDragDropMode(QAbstractItemView::DragOnly);
    effects_->setIconSize(QSize(26, 18));
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(categories_);
    split->addWidget(effects_);
    split->setChildrenCollapsible(false);
    split->setSizes({200, 240});

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(tabs_);
    auto* findRow = new QHBoxLayout;
    findRow->addWidget(find_);
    findRow->addStretch(1);
    layout->addLayout(findRow);
    layout->addWidget(split, 1);

    connect(tabs_, &QTabBar::currentChanged, this, [this] { fillCategories(); });
    connect(categories_, &QListWidget::currentRowChanged, this, [this] { fillEffects(); });
    connect(find_, &QLineEdit::textChanged, this, [this] { fillEffects(); });
    connect(effects_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (const auto e = effectAt(effects_->row(item))) emit effectActivated(*e);
    });
    fillCategories();
}

bool EffectPalette::tabShows(const EffectInfo& effect) const {
    const QList<EffectCatalog::Tab>& tabs = EffectCatalog::builtin().tabs();
    const int i = tabs_->currentIndex();
    if (i < 0 || i >= tabs.size()) return false;
    const QString& kind = tabs.at(i).kind;
    // Filters lists every video effect, as in the picture on p.37 (where
    // Illusion FX shows its transitions too); Transitions only those that
    // apply to transitions; Audio Track the audio track effects (p.792).
    // Audio Clip effects come later.
    if (kind == QLatin1String("audioTrack")) return !effect.audioEffect.isEmpty();
    if (!effect.audioEffect.isEmpty()) return false;
    if (kind == QLatin1String("video")) return true;
    if (kind == QLatin1String("transitions")) return effect.transition;
    return false;
}

// The current tab's effects: the catalog's, or the audio track effects.
QList<EffectInfo> EffectPalette::tabEffects() const {
    const QList<EffectCatalog::Tab>& tabs = EffectCatalog::builtin().tabs();
    const int i = tabs_->currentIndex();
    if (i >= 0 && i < tabs.size() && tabs.at(i).kind == QLatin1String("audioTrack")) return audioTrackEffects();
    return EffectCatalog::builtin().all();
}

void EffectPalette::fillCategories() {
    const QString was = category();
    categories_->blockSignals(true);
    categories_->clear();
    categories_->addItem(kAll);
    QStringList names = EffectCatalog::builtin().categories();
    for (const EffectInfo& e : tabEffects()) {
        if (!names.contains(e.category)) names << e.category;
    }
    const QList<EffectInfo> effects = tabEffects();
    for (const QString& c : names) {
        bool shown = tabs_->currentIndex() == 0; // Filters lists every category (p.37)
        for (const EffectInfo& e : effects) shown = shown || (e.category == c && tabShows(e));
        if (shown) categories_->addItem(c);
    }
    const auto found = categories_->findItems(was, Qt::MatchExactly);
    categories_->setCurrentItem(found.isEmpty() ? categories_->item(0) : found.first());
    categories_->blockSignals(false);
    fillEffects();
}

// Effects that apply only to transitions get the transition icon. In "All" the tooltip names the
// category too.
void EffectPalette::fillEffects() {
    effects_->clear();
    const QString cat = category();
    const QString text = find_->text().trimmed();
    QList<EffectInfo> list;
    for (const EffectInfo& e : tabEffects()) {
        if (!tabShows(e)) continue;
        if (cat != kAll && e.category != cat) continue;
        if (!text.isEmpty() && !e.name.contains(text, Qt::CaseInsensitive)) continue;
        list << e;
    }
    for (const EffectInfo& e : list) {
        auto* item = new QListWidgetItem(effectIcon(e.transition && !e.segment, e.realTime), e.name, effects_);
        item->setData(Qt::UserRole, e.category);
        item->setData(Qt::UserRole + 1, e.audioEffect);
        item->setToolTip(cat == kAll ? e.category + QStringLiteral(": ") + e.name : e.name);
    }
}

void EffectPalette::setTab(int index) {
    tabs_->setCurrentIndex(index);
}

void EffectPalette::setCategory(const QString& name) {
    const auto found = categories_->findItems(name, Qt::MatchExactly);
    if (!found.isEmpty()) categories_->setCurrentItem(found.first());
}

QString EffectPalette::category() const {
    return categories_->currentItem() ? categories_->currentItem()->text() : kAll;
}

void EffectPalette::setQuickFind(const QString& text) {
    find_->setText(text);
}

QStringList EffectPalette::shownEffects() const {
    QStringList out;
    for (int i = 0; i < effects_->count(); ++i) out << effects_->item(i)->text();
    return out;
}

std::optional<EffectInfo> EffectPalette::effectAt(int row) const {
    const QListWidgetItem* item = effects_->item(row);
    if (!item) return std::nullopt;
    for (const EffectInfo& a : audioTrackEffects()) {
        if (a.audioEffect == item->data(Qt::UserRole + 1).toString() && !a.audioEffect.isEmpty()) return a;
    }
    const EffectInfo* e = EffectCatalog::builtin().find(item->data(Qt::UserRole).toString(), item->text());
    if (!e) return std::nullopt;
    return *e;
}

void EffectPalette::contextMenuEvent(QContextMenuEvent* event) {
    // Set Font (p.38).
    QMenu menu(this);
    menu.addAction(tr("Set Font..."), this, [this] {
        bool ok = false;
        const QFont f = QFontDialog::getFont(&ok, categories_->font(), this, tr("Set Font"));
        if (!ok) return;
        categories_->setFont(f);
        effects_->setFont(f);
    });
    menu.exec(event->globalPos());
}

} // namespace lumenis::ui
