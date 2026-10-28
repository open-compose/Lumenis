#include "lumenis/model/bin_relations.h"

#include "lumenis/model/sequence.h"

#include <string>

// Bin > Select's relations (bin_relations.h). Media are named by the MobID string of the master clip
// that holds them; references by item id cover segments and clips without MobIDs.

namespace lumenis::model {

namespace {

bool isClip(BinItemKind kind) {
    return kind == BinItemKind::MasterClip || kind == BinItemKind::AudioClip || kind == BinItemKind::Subclip ||
           kind == BinItemKind::Effect || kind == BinItemKind::MotionEffect || kind == BinItemKind::RenderedEffect ||
           kind == BinItemKind::Title;
}

QString mobKey(const MobId& mob) {
    return QString::fromStdString(mob.toString());
}

// What an item refers to: MobID strings and item ids of other objects (not itself).
struct Refs {
    QSet<QString> mobs;
    QSet<QString> ids;
};

Refs refsOf(const BinItem& item) {
    Refs r;
    for (const ClipSource& s : item.sources) {
        if (s.kind == ClipSource::Kind::Clip && !s.mob.isNull()) r.mobs.insert(mobKey(s.mob));
    }
    if (item.kind == BinItemKind::Sequence) {
        const Sequence seq = Sequence::fromItem(item);
        for (const QString& name : seq.trackNames()) {
            for (const Segment& seg : seq.track(name)->segments) {
                if (seg.isFiller()) continue;
                if (!seg.clipId.isEmpty()) r.ids.insert(seg.clipId);
                r.mobs.insert(mobKey(seg.sourceMob.isNull() ? derivedMobId(seg.clipId) : seg.sourceMob));
            }
        }
    }
    return r;
}

// Every object of the open bins with its own MobID string and what it refers to.
struct Entry {
    QString bin;
    const BinItem* item = nullptr;
    QString mob;
    Refs refs;
};

QList<Entry> entries(const QList<OpenBin>& bins) {
    QList<Entry> out;
    for (const OpenBin& b : bins) {
        if (!b.bin) continue;
        for (const BinItem& item : b.bin->items) out.append({b.id, &item, mobKey(mobIdOf(item)), refsOf(item)});
    }
    return out;
}

bool selectedIn(const BinSelection& selected, const Entry& e) {
    return selected.value(e.bin).contains(e.item->id);
}

// The media an entry uses: a master clip its own; a subclip its master's; a sequence its clips'
// media, resolved one level through subclips.
QSet<QString> mediaOf(const Entry& e, const QList<Entry>& all) {
    QSet<QString> media;
    if (e.item->kind != BinItemKind::Sequence && e.item->kind != BinItemKind::Subclip) media.insert(e.mob);
    QSet<QString> refs = e.refs.mobs;
    for (const Entry& other : all) {
        if (!e.refs.ids.contains(other.item->id) && !e.refs.mobs.contains(other.mob)) continue;
        if (other.item->kind == BinItemKind::Subclip) {
            refs += other.refs.mobs;
        } else {
            refs.insert(other.mob);
        }
    }
    return media + refs;
}

void add(BinSelection& out, const Entry& e) {
    QStringList& ids = out[e.bin];
    if (!ids.contains(e.item->id)) ids << e.item->id;
}

} // namespace

BinSelection mediaRelatives(const QList<OpenBin>& bins, const BinSelection& selected) {
    const QList<Entry> all = entries(bins);
    QSet<QString> media;
    for (const Entry& e : all) {
        if (selectedIn(selected, e)) media += mediaOf(e, all);
    }
    BinSelection out;
    for (const Entry& e : all) {
        if (selectedIn(selected, e) || mediaOf(e, all).intersects(media)) add(out, e);
    }
    return out;
}

BinSelection sources(const QList<OpenBin>& bins, const BinSelection& selected) {
    const QList<Entry> all = entries(bins);
    QSet<QString> mobs;
    QSet<QString> ids;
    for (const Entry& e : all) {
        if (!selectedIn(selected, e)) continue;
        mobs += e.refs.mobs;
        ids += e.refs.ids;
    }
    // A subclip found as a source brings its master clip.
    for (const Entry& e : all) {
        if (e.item->kind == BinItemKind::Subclip && (ids.contains(e.item->id) || mobs.contains(e.mob))) mobs += e.refs.mobs;
    }
    BinSelection out;
    for (const Entry& e : all) {
        if (!isClip(e.item->kind)) continue;
        if (ids.contains(e.item->id) || mobs.contains(e.mob)) add(out, e);
    }
    return out;
}

BinSelection unreferencedClips(const QList<OpenBin>& bins) {
    const QList<Entry> all = entries(bins);
    QSet<QString> mobs;
    QSet<QString> ids;
    for (const Entry& e : all) {
        mobs += e.refs.mobs;
        ids += e.refs.ids;
    }
    BinSelection out;
    for (const Entry& e : all) {
        if (!isClip(e.item->kind)) continue;
        if (!ids.contains(e.item->id) && !mobs.contains(e.mob)) add(out, e);
    }
    return out;
}

} // namespace lumenis::model
