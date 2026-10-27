// The Audio Mixer's track sidebar (Editing Guide p.691) and Audio Grouping (p.696-698;
// docs/spec/audio-mixer.md): the Tracks list that chooses which strips the mixer shows, the Groups
// pane, the Audio Track Grouping window (AudioGroupDialog), and the track numbers its text box takes.
// The rest of the mixer is in audio_tools.cpp; track selection goes through the Timeline's record
// track buttons, as the strips' track buttons do (p.698).
#include "lumenis/ui/audio_tools.h"

#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

namespace {

int trackNumber(const QString& track) {
    return track.mid(1).toInt();
}

QList<int> numbersOf(const QStringList& tracks) {
    QList<int> out;
    for (const QString& t : tracks) out << trackNumber(t);
    return out;
}

// Track names sorted by number.
QStringList sortedTracks(QStringList tracks) {
    std::sort(tracks.begin(), tracks.end(), [](const QString& a, const QString& b) { return trackNumber(a) < trackNumber(b); });
    tracks.removeDuplicates();
    return tracks;
}

} // namespace

std::optional<QList<int>> parseTrackNumbers(const QString& text) {
    QList<int> out;
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    static const QRegularExpression item(QStringLiteral("^(\\d+)(?:-(\\d+))?$"));
    for (const QString& part : text.split(separators, Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = item.match(part);
        if (!m.hasMatch()) return std::nullopt;
        const int from = m.captured(1).toInt();
        const int to = m.captured(2).isEmpty() ? from : m.captured(2).toInt();
        if (from < 1 || to < from || to - from > 1000) return std::nullopt;
        for (int n = from; n <= to; ++n) out << n;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

QString trackNumbersText(QList<int> numbers) {
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    QStringList parts;
    for (qsizetype i = 0; i < numbers.size();) {
        qsizetype j = i;
        while (j + 1 < numbers.size() && numbers.at(j + 1) == numbers.at(j) + 1) ++j;
        parts << (j == i ? QString::number(numbers.at(i)) : QStringLiteral("%1-%2").arg(numbers.at(i)).arg(numbers.at(j)));
        i = j + 1;
    }
    return parts.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------- AudioGroupDialog

// Laid out as the picture on p.698: the name, the text box with Apply and Restore beside it, the
// track check boxes, OK and Cancel. The check boxes and the text follow each other: a click rewrites
// the text, Apply reads it back.
AudioGroupDialog::AudioGroupDialog(const AudioGroup& group, const QStringList& tracks, QWidget* parent)
    : QDialog(parent), initial_(group) {
    setObjectName(QStringLiteral("audioTrackGrouping"));
    setWindowTitle(tr("Audio Track Grouping"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Group Name:"), this));
    name_ = new QLineEdit(group.name, this);
    name_->setObjectName(QStringLiteral("groupName"));
    layout->addWidget(name_);
    layout->addWidget(new QLabel(tr("Select Tracks in Group:"), this));
    auto* row = new QHBoxLayout;
    text_ = new QLineEdit(this);
    text_->setObjectName(QStringLiteral("groupTracks"));
    row->addWidget(text_, 1);
    auto* buttons = new QVBoxLayout;
    auto* apply = new QPushButton(tr("Apply"), this);
    apply->setObjectName(QStringLiteral("apply"));
    auto* restoreButton = new QPushButton(tr("Restore"), this);
    restoreButton->setObjectName(QStringLiteral("restore"));
    buttons->addWidget(apply);
    buttons->addWidget(restoreButton);
    row->addLayout(buttons);
    layout->addLayout(row);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("groupTrackList"));
    for (const QString& t : sortedTracks(tracks + group.tracks)) {
        auto* item = new QListWidgetItem(t, list_);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    }
    layout->addWidget(list_, 1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, [this] { this->apply(); });
    connect(restoreButton, &QPushButton::clicked, this, &AudioGroupDialog::restore);
    connect(list_, &QListWidget::itemChanged, this, [this] { text_->setText(trackNumbersText(numbersOf(this->group().tracks))); });
    restore();
}

AudioGroup AudioGroupDialog::group() const {
    AudioGroup g = initial_;
    g.name = name_->text().trimmed();
    g.tracks.clear();
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->checkState() == Qt::Checked) g.tracks << list_->item(i)->text();
    }
    return g;
}

bool AudioGroupDialog::apply() {
    const auto numbers = parseTrackNumbers(text_->text());
    if (!numbers) return false;
    const QSignalBlocker block(list_);
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        item->setCheckState(numbers->contains(trackNumber(item->text())) ? Qt::Checked : Qt::Unchecked);
    }
    text_->setText(trackNumbersText(*numbers));
    return true;
}

void AudioGroupDialog::restore() {
    name_->setText(initial_.name);
    text_->setText(trackNumbersText(numbersOf(initial_.tracks)));
    apply();
}

// ---------------------------------------------------------------- the mixer's sidebar

// The sidebar of the picture on p.697: the Tracks list (the highest track at the top, a check box
// each) and the Groups pane below it. Hidden until Show Track Sidebar.
QWidget* AudioMixer::makeSidebar() {
    sidebar_ = new QWidget(this);
    sidebar_->setObjectName(QStringLiteral("trackSidebar"));
    sidebar_->setFixedWidth(150);
    auto* v = new QVBoxLayout(sidebar_);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    v->addWidget(new QLabel(tr("Tracks"), sidebar_));
    sidebarTracks_ = new QListWidget(sidebar_);
    sidebarTracks_->setObjectName(QStringLiteral("sidebarTracks"));
    connect(sidebarTracks_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        setTrackShown(item->text(), item->checkState() == Qt::Checked);
    });
    v->addWidget(sidebarTracks_, 2);
    v->addWidget(new QLabel(tr("Groups"), sidebar_));
    groupList_ = new QListWidget(sidebar_);
    groupList_->setObjectName(QStringLiteral("groups"));
    groupList_->setSelectionMode(QAbstractItemView::NoSelection);
    groupList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(groupList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        clickGroup(groupList_->row(item), QApplication::keyboardModifiers() & Qt::AltModifier);
    });
    connect(groupList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QListWidgetItem* item = groupList_->itemAt(at);
        groupMenu(item ? groupList_->row(item) : -1)->exec(groupList_->viewport()->mapToGlobal(at));
    });
    v->addWidget(groupList_, 1);
    groupMenu_ = new QMenu(this);
    sidebar_->hide();
    return sidebar_;
}

bool AudioMixer::isSidebarShown() const {
    return !sidebar_->isHidden();
}

void AudioMixer::setSidebarShown(bool shown) {
    sidebar_->setVisible(shown);
    sidebarButton_->setText(shown ? QStringLiteral("<<") : QStringLiteral(">>"));
    sidebarButton_->setToolTip(shown ? tr("Hide Track Sidebar") : tr("Show Track Sidebar"));
}

void AudioMixer::setTrackShown(const QString& track, bool shown) {
    if (shown) {
        hidden_.remove(track);
    } else {
        hidden_.insert(track);
    }
    if (MixerStrip* s = strips_.value(track)) s->setVisible(shown);
    syncSidebar();
}

// The Groups pane shows a selected group highlighted and a ganged one with its letter; dimmed while
// groups are disabled.
void AudioMixer::syncSidebar() {
    {
        const QSignalBlocker block(sidebarTracks_);
        sidebarTracks_->clear();
        for (auto it = order_.crbegin(); it != order_.crend(); ++it) {
            auto* item = new QListWidgetItem(*it, sidebarTracks_);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(isTrackShown(*it) ? Qt::Checked : Qt::Unchecked);
        }
    }
    groupList_->clear();
    for (const AudioGroup& g : std::as_const(groups_)) {
        auto* item = new QListWidgetItem(g.gang.isEmpty() ? g.name : QStringLiteral("%1 (%2)").arg(g.name, g.gang), groupList_);
        item->setToolTip(trackNumbersText(numbersOf(g.tracks)));
        QFont f = item->font();
        f.setBold(g.selected);
        item->setFont(f);
        item->setFlags(groupsDisabled_ ? Qt::NoItemFlags : Qt::ItemIsEnabled);
    }
}

QMenu* AudioMixer::groupMenu(int index) {
    QMenu* m = groupMenu_;
    m->clear();
    const bool on = index >= 0 && index < groups_.size();
    if (on) {
        m->addAction(tr("Modify the Group"), this, [this, index] { modifyGroup(index); });
        m->addAction(tr("Duplicate the Group"), this, [this, index] { duplicateGroup(index); });
        m->addAction(tr("Delete the Group"), this, [this, index] { removeGroup(index); });
        const bool ganged = !groups_.at(index).gang.isEmpty();
        m->addAction(ganged ? tr("Ungang the Group") : tr("Gang the Group"), this,
                     [this, index, ganged] { setGroupGanged(index, !ganged); });
        m->addSeparator();
    }
    QAction* add = m->addAction(tr("New Group from Selected Tracks"), this, [this] { newGroupFromSelectedTracks(); });
    add->setEnabled(!selectedTracks().isEmpty());
    QAction* disable = m->addAction(tr("Disable All Groups"), this, [this](bool checked) { setGroupsDisabled(checked); });
    disable->setCheckable(true);
    disable->setChecked(groupsDisabled_);
    return m;
}

int AudioMixer::newGroupFromSelectedTracks() {
    AudioGroup g;
    g.name = tr("Untitled");
    g.tracks = selectedTracks();
    const auto made = groupAsk_ ? groupAsk_(g, order_) : [&]() -> std::optional<AudioGroup> {
        AudioGroupDialog dialog(g, order_, this);
        if (dialog.exec() != QDialog::Accepted) return std::nullopt;
        return dialog.group();
    }();
    if (!made || made->tracks.isEmpty()) return -1;
    return addGroup(*made);
}

bool AudioMixer::modifyGroup(int index) {
    if (index < 0 || index >= groups_.size()) return false;
    const AudioGroup& g = groups_.at(index);
    const auto made = groupAsk_ ? groupAsk_(g, order_) : [&]() -> std::optional<AudioGroup> {
        AudioGroupDialog dialog(g, order_, this);
        if (dialog.exec() != QDialog::Accepted) return std::nullopt;
        return dialog.group();
    }();
    if (!made) return false;
    const QString gang = groups_.at(index).gang;
    if (!gang.isEmpty()) setGroupGanged(index, false);
    groups_[index].name = made->name.isEmpty() ? groups_.at(index).name : made->name;
    groups_[index].tracks = sortedTracks(made->tracks);
    if (!gang.isEmpty()) setGroupGanged(index, true);
    syncSidebar();
    return true;
}

int AudioMixer::addGroup(const AudioGroup& group) {
    AudioGroup g = group;
    g.tracks = sortedTracks(g.tracks);
    g.selected = false;
    g.gang.clear();
    if (g.name.isEmpty()) g.name = tr("Untitled");
    groups_ << g;
    syncSidebar();
    return static_cast<int>(groups_.size() - 1);
}

void AudioMixer::duplicateGroup(int index) {
    if (index < 0 || index >= groups_.size()) return;
    AudioGroup copy = groups_.at(index);
    copy.name = tr("%1 Copy").arg(copy.name);
    addGroup(copy);
}

void AudioMixer::removeGroup(int index) {
    if (index < 0 || index >= groups_.size()) return;
    setGroupGanged(index, false);
    groups_.removeAt(index);
    syncSidebar();
}

void AudioMixer::setGroupGanged(int index, bool ganged) {
    if (index < 0 || index >= groups_.size()) return;
    AudioGroup& g = groups_[index];
    if (!ganged) {
        for (const QString& t : std::as_const(g.tracks)) {
            if (gangs_.value(t) == g.gang) setGang(t, QString());
        }
        g.gang.clear();
    } else if (g.gang.isEmpty()) {
        // The first of gangs A-J (p.704) no track or other group uses.
        QSet<QString> used(gangs_.cbegin(), gangs_.cend());
        for (const AudioGroup& other : std::as_const(groups_)) used.insert(other.gang);
        for (char c = 'A'; c <= 'J'; ++c) {
            const QString letter(QLatin1Char{c});
            if (used.contains(letter)) continue;
            g.gang = letter;
            break;
        }
        if (g.gang.isEmpty()) return; // all ten are in use
        for (const QString& t : std::as_const(g.tracks)) setGang(t, g.gang);
    }
    syncSidebar();
}

void AudioMixer::setGroupsDisabled(bool disabled) {
    groupsDisabled_ = disabled;
    syncSidebar();
}

void AudioMixer::selectTracks(const QSet<QString>& tracks, bool replace) {
    for (const QString& t : std::as_const(order_)) {
        const bool want = tracks.contains(t) || (!replace && timeline_.patch().isRecordSelected(t));
        if (timeline_.patch().isRecordSelected(t) != want) timeline_.toggleRecordTrack(t);
    }
}

// p.696's four rules. The tracks a group names but the sequence lacks are left alone.
void AudioMixer::clickGroup(int index, bool alt) {
    if (groupsDisabled_ || index < 0 || index >= groups_.size()) return;
    AudioGroup& g = groups_[index];
    const QSet<QString> own(g.tracks.cbegin(), g.tracks.cend());
    if (!g.selected) {
        if (alt) {
            for (AudioGroup& other : groups_) other.selected = false;
        }
        g.selected = true;
        selectTracks(own, alt);
    } else if (alt) {
        // Deselecting with Alt deselects every audio track.
        for (AudioGroup& other : groups_) other.selected = false;
        selectTracks({}, true);
    } else {
        g.selected = false;
        QSet<QString> keep;
        for (const AudioGroup& other : std::as_const(groups_)) {
            if (other.selected) keep.unite(QSet<QString>(other.tracks.cbegin(), other.tracks.cend()));
        }
        for (const QString& t : std::as_const(order_)) {
            if (own.contains(t) && !keep.contains(t) && timeline_.patch().isRecordSelected(t)) timeline_.toggleRecordTrack(t);
        }
    }
    syncSidebar();
}

} // namespace lumenis::ui
