// The Timeline Quick Find (Phase 7.4b2; docs/spec/timeline-find.md; Editing Guide p.645-646): the search field at the
// foot of the Timeline, its menu of what to search and the Find to the left and right buttons. Closed caption text is
// part of the Visible Timeline Text ("Make sure that Visible Timeline Text is selected in the search criteria", p.985).
#include "lumenis/model/marker.h"
#include "lumenis/ui/timeline.h"

#include <QLineEdit>
#include <QMenu>

#include <algorithm>

namespace lumenis::ui {

void Timeline::setFindText(const QString& text) {
    search_->setText(text);
}

QString Timeline::findText() const {
    return search_->text();
}

QList<Timeline::FindPlace> Timeline::findPlaces() const {
    QList<FindPlace> out;
    const FindOptions& o = findOptions_;
    const model::Sequence& seq = shown_;
    for (const model::Track& t : seq.tracks()) {
        std::int64_t at = 0;
        for (const model::Segment& s : t.segments) {
            if (!s.isFiller()) {
                QStringList texts;
                // Visible Timeline Text: the Clip Text the Timeline shows on the segment (p.592).
                if (o.visibleText) texts << clipText(t, s);
                if (o.clipName) {
                    texts << s.name;
                    if (const auto clip = composer_.resolveForSegment(s)) texts << clip->name;
                }
                // Resolution: the clip's Video column (its media's resolution).
                if (o.resolution) {
                    if (const auto clip = composer_.resolveForSegment(s)) texts << clip->fields.value(QStringLiteral("Video"));
                }
                if (o.clipNotes) texts << s.note;
                texts.removeAll(QString());
                if (!texts.isEmpty()) out << FindPlace{at, texts.join(QLatin1Char('\n'))};
            }
            at += s.length;
        }
    }
    // The captions D1 shows, each at its start (ours: a caption clip is one long segment).
    if (o.visibleText) {
        for (const model::ShownCaption& c : captionTrack().shown) out << FindPlace{c.start, c.screen.text()};
    }
    if (o.markers) {
        if (const model::LoadedClip* loaded = composer_.state(shownMonitor()).current()) {
            for (const model::Marker& m : model::markersOf(loaded->item)) {
                out << FindPlace{m.frame, QStringList{m.name, m.comment}.join(QLatin1Char('\n'))};
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const FindPlace& a, const FindPlace& b) { return a.frame < b.frame; });
    return out;
}

bool Timeline::findNext(bool forward) {
    const QString query = findText().trimmed();
    model::MonitorState& state = composer_.state(shownMonitor());
    if (query.isEmpty() || state.isEmpty()) return false;
    const std::int64_t from = state.position();
    const QList<FindPlace> places = findPlaces();
    std::optional<std::int64_t> found;
    for (const FindPlace& p : places) {
        if (!p.text.contains(query, Qt::CaseInsensitive)) continue;
        if (forward && p.frame > from && (!found || p.frame < *found)) found = p.frame;
        if (!forward && p.frame < from && (!found || p.frame > *found)) found = p.frame;
    }
    if (!found) return false;
    state.setPosition(*found);
    return true;
}

QMenu* Timeline::findOptionsMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("timelineSearchMenu"));
    QAction* none = menu->addAction(tr("None"));
    none->setObjectName(QStringLiteral("findNone"));
    connect(none, &QAction::triggered, this, [this] { findOptions_ = FindOptions{false, false, false, false, false}; });
    menu->addSeparator();
    auto option = [&](const QString& text, const char* name, bool FindOptions::* member) {
        QAction* a = menu->addAction(text);
        a->setObjectName(QString::fromLatin1(name));
        a->setCheckable(true);
        a->setChecked(findOptions_.*member);
        connect(a, &QAction::toggled, this, [this, member](bool on) { findOptions_.*member = on; });
    };
    option(tr("Visible Timeline Text"), "findVisibleText", &FindOptions::visibleText);
    menu->addSeparator();
    option(tr("Resolution"), "findResolution", &FindOptions::resolution);
    option(tr("Clip Name"), "findClipName", &FindOptions::clipName);
    option(tr("Timeline Clip Notes"), "findClipNotes", &FindOptions::clipNotes);
    option(tr("Markers"), "findMarkers", &FindOptions::markers);
    menu->addSeparator();
    QAction* all = menu->addAction(tr("All"));
    all->setObjectName(QStringLiteral("findAll"));
    connect(all, &QAction::triggered, this, [this] { findOptions_ = FindOptions{true, true, true, true, true}; });
    return menu;
}

} // namespace lumenis::ui
