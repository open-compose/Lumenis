// The Timeline's MultiRez button (8.6b; Editing Guide p.1111-1116, docs/spec/dynamic-relink.md): its three displays,
// the button's colour, its right-click menu, Find Next Mismatched or Unavailable Clip and Update Media Status. What a
// clip's media are and whether they match comes from MainWindow through the ResolutionResolver (Dynamic Relink,
// media::chooseMedia); the Clip Color dialog's Resolution Tracking colours the segments (model::timelineClipColor).
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QToolButton>

namespace lumenis::ui {

namespace {

// The button's picture (p.1112): a diamond, its bottom half black for Show Mismatches, its top half black for Show
// Target Availability, on a blue, yellow or gray background. The drawing is ours.
QIcon multiRezIcon(Timeline::MultiRezDisplay display, const QString& color) {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor background = color == QLatin1String("blue")     ? QColor(0x2d, 0x6e, 0xe0)
                              : color == QLatin1String("yellow") ? QColor(0xe6, 0xc6, 0x19)
                                                                 : QColor(0x80, 0x80, 0x80);
    p.setPen(Qt::NoPen);
    p.setBrush(background);
    p.drawRoundedRect(QRectF(3, 3, 26, 26), 4, 4);
    QPainterPath diamond;
    diamond.moveTo(16, 6);
    diamond.lineTo(26, 16);
    diamond.lineTo(16, 26);
    diamond.lineTo(6, 16);
    diamond.closeSubpath();
    p.setBrush(QColor(0xd8, 0xd8, 0xd8));
    p.drawPath(diamond);
    if (display != Timeline::MultiRezDisplay::Inactive) {
        p.save();
        p.setClipRect(display == Timeline::MultiRezDisplay::Mismatches ? QRectF(0, 16, 32, 16) : QRectF(0, 0, 32, 16));
        p.setBrush(Qt::black);
        p.drawPath(diamond);
        p.restore();
    }
    p.setPen(QPen(Qt::black, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(diamond);
    p.end();
    pm.setDevicePixelRatio(2.0);
    return QIcon(pm);
}

bool marked(model::ClipFacts::Resolution r) {
    return r == model::ClipFacts::Resolution::Mismatched || r == model::ClipFacts::Resolution::Offline ||
           r == model::ClipFacts::Resolution::Unavailable;
}

} // namespace

void Timeline::setMultiRezDisplay(MultiRezDisplay display) {
    multiRez_ = display;
    refreshMultiRezButton(shown());
    view_->update();
}

void Timeline::setDynamicRelinkState(bool enabled, bool overrideWorking) {
    dynamicRelinkOn_ = enabled;
    overrideWorking_ = overrideWorking;
    view_->update();
}

void Timeline::setShowMismatchedRenderRanges(bool on) {
    mismatchedRenders_ = on;
    view_->update();
}

bool Timeline::renderMismatched(const model::EffectRender& render) const {
    return renderResolutionTest_ && renderResolutionTest_(render);
}

model::ClipFacts::Resolution Timeline::segmentResolution(const model::Segment& segment) const {
    if (multiRez_ == MultiRezDisplay::Inactive || segment.isFiller()) return model::ClipFacts::Resolution::None;
    if (!resolutionResolver_) return model::ClipFacts::Resolution::Matched;
    return resolutionResolver_(segment.binId, segment.clipId, multiRez_ == MultiRezDisplay::TargetAvailability);
}

// "If the background is blue, some media does not match or is not available in the current settings ... If the
// background is yellow, some media is not available in the target settings" (p.1112); gray otherwise.
void Timeline::refreshMultiRezButton(const model::Sequence& seq) const {
    QString color = QStringLiteral("gray");
    if (multiRez_ != MultiRezDisplay::Inactive) {
        bool any = false;
        for (const model::Track& t : seq.tracks()) {
            for (const model::Segment& s : t.segments) {
                if (marked(segmentResolution(s))) any = true;
                // Effects rendered at another resolution count in the Show Mismatches display (p.1115).
                if (multiRez_ == MultiRezDisplay::Mismatches && mismatchedRenders_ &&
                    ((s.render && renderMismatched(*s.render)) || (s.transitionRender && renderMismatched(*s.transitionRender)))) {
                    any = true;
                }
                if (any) break;
            }
            if (any) break;
        }
        if (any) color = multiRez_ == MultiRezDisplay::Mismatches ? QStringLiteral("blue") : QStringLiteral("yellow");
    }
    if (color == multiRezColor_ && toolButton(QStringLiteral("multiRez"))->property("display").toInt() ==
                                       static_cast<int>(multiRez_)) {
        return;
    }
    multiRezColor_ = color;
    showMultiRezColor();
}

void Timeline::showMultiRezColor() const {
    QToolButton* b = toolButton(QStringLiteral("multiRez"));
    if (!b) return;
    b->setProperty("display", static_cast<int>(multiRez_));
    b->setIcon(multiRezIcon(multiRez_, multiRezColor_));
    b->setToolTip(multiRez_ == MultiRezDisplay::Mismatches           ? tr("MultiRez: Show Mismatches")
                  : multiRez_ == MultiRezDisplay::TargetAvailability ? tr("MultiRez: Show Target Availability")
                                                                     : tr("MultiRez"));
}

// "In the Show Mismatches display, moves the blue position indicator to the next item that is not in the working or
// target settings. This could be a clip or a rendered effect. In the Show Target Availability display, moves the blue
// position indicator to the next clip that is not available in the target settings ... This option works only on
// enabled tracks" (p.1115).
bool Timeline::findNextMismatched() {
    message_.clear();
    if (multiRez_ == MultiRezDisplay::Inactive) {
        return fail(tr("Click the MultiRez button to show mismatches or target availability first."));
    }
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList tracks = patch_.selectedRecordTracks(seq);
    if (tracks.isEmpty()) return fail(tr("Select the tracks to search."));
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    std::optional<std::int64_t> next;
    for (const QString& name : tracks) {
        const model::Track* t = seq.track(name);
        if (!t) continue;
        std::int64_t at = 0;
        for (const model::Segment& s : t->segments) {
            bool hit = marked(segmentResolution(s));
            if (multiRez_ == MultiRezDisplay::Mismatches && s.render && renderMismatched(*s.render)) hit = true;
            if (hit && at > position && (!next || at < *next)) next = at;
            at += s.length;
        }
    }
    if (!next) {
        return fail(multiRez_ == MultiRezDisplay::Mismatches ? tr("There are no more mismatched clips.")
                                                             : tr("There are no more unavailable clips."));
    }
    composer_.stop();
    composer_.setActiveMonitor(MonitorId::Record);
    composer_.moveTo(MonitorId::Record, *next);
    return true;
}

void Timeline::updateMediaStatus() {
    emit updateMediaStatusRequested();
    refreshMultiRezButton(shown());
    view_->update();
}

// The MultiRez Button Menu's options in the guide's table order (p.1114-1116).
QMenu* Timeline::multiRezMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("multiRezMenu"));
    auto check = [menu](const QString& text, bool on, const char* name) {
        QAction* a = menu->addAction(text);
        a->setObjectName(QString::fromLatin1(name));
        a->setCheckable(true);
        a->setChecked(on);
        return a;
    };
    connect(check(tr("Enable Clip Coloring"), clipColors_.resolution, "multiRezClipColoring"), &QAction::toggled, this,
            [this](bool on) {
                model::ClipColorOptions o = clipColors_;
                o.resolution = on;
                setClipColorOptions(o);
            });
    connect(check(tr("Show Mismatched Render Ranges"), mismatchedRenders_, "multiRezRenderRanges"), &QAction::toggled,
            this, [this](bool on) { setShowMismatchedRenderRanges(on); });
    menu->addSeparator();
    connect(check(tr("Show Mismatches"), multiRez_ == MultiRezDisplay::Mismatches, "multiRezMismatches"),
            &QAction::triggered, this, [this] { setMultiRezDisplay(MultiRezDisplay::Mismatches); });
    connect(check(tr("Show Target Availability"), multiRez_ == MultiRezDisplay::TargetAvailability, "multiRezTarget"),
            &QAction::triggered, this, [this] { setMultiRezDisplay(MultiRezDisplay::TargetAvailability); });
    QAction* find = menu->addAction(tr("Find Next Mismatched or Unavailable Clip"));
    find->setObjectName(QStringLiteral("multiRezFindNext"));
    find->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));
    find->setEnabled(multiRez_ != MultiRezDisplay::Inactive);
    connect(find, &QAction::triggered, this, [this] { findNextMismatched(); });
    QAction* update = menu->addAction(tr("Update Media Status"));
    update->setObjectName(QStringLiteral("multiRezUpdate"));
    connect(update, &QAction::triggered, this, &Timeline::updateMediaStatus);
    menu->addSeparator();
    connect(check(tr("Enable Dynamic Relink"), dynamicRelinkOn_, "multiRezEnable"), &QAction::toggled, this,
            [this](bool on) { emit dynamicRelinkEnableRequested(on); });
    QAction* override = check(tr("Override Working Settings with Target Settings"), overrideWorking_, "multiRezOverride");
    override->setEnabled(dynamicRelinkOn_);
    connect(override, &QAction::toggled, this, [this](bool on) { emit overrideWorkingRequested(on); });
    QAction* settings = menu->addAction(tr("Dynamic Relink Settings..."));
    settings->setObjectName(QStringLiteral("multiRezSettings"));
    connect(settings, &QAction::triggered, this, &Timeline::dynamicRelinkSettingsRequested);
    return menu;
}

} // namespace lumenis::ui
