// MainWindow's Dynamic Relink (8.6a; Editing Guide ch.31, p.1102-1110; docs/spec/dynamic-relink.md): the Project
// setting read into media::DynamicRelinkSettings, clips relinked as they load into the Source monitor or the Timeline
// (the Composer's load filter), and the loaded clips relinked again when the setting is applied: "Selecting this option
// and clicking 'Apply' or 'OK' will trigger immediate relink for any media loaded into Source Monitor or the Timeline"
// (p.1170). The choosing itself is media::dynamicRelink.
#include "lumenis/ui/main_window.h"

#include "lumenis/media/link.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QSet>

namespace lumenis::ui {

media::DynamicRelinkSettings MainWindow::dynamicRelinkSettings() const {
    QJsonObject values;
    const SettingKind* kind = SettingsSchema::builtIn().find(QStringLiteral("Dynamic Relink"));
    if (kind && settings_) {
        for (const SettingTab& t : kind->tabs) {
            for (const SettingOption& o : t.options) {
                const QJsonValue v = settings_->value(kind->kind, o.key);
                if (!v.isUndefined() && !v.isNull()) values.insert(o.key, v);
            }
        }
    }
    return media::DynamicRelinkSettings::fromValues(values);
}

// One clip: relinked by the setting, or back on its affinity media while the setting is off. Stored in its bin when it
// changed, so the sequences that use it and the monitors play the new media.
bool MainWindow::dynamicRelinkClip(const QString& binId, model::BinItem& clip, const media::DynamicRelinkSettings& s) {
    if (!project_) return false;
    const QSize raster(project_->settings.format.width, project_->settings.format.height);
    const bool changed = s.enabled ? media::dynamicRelink(clip, mediaDb_, s, project_->settings.format.editRate, raster)
                                   : media::restoreAffinityMedia(clip);
    if (changed) {
        storeClip(binId, clip);
        resolutionCache_.clear();
    }
    return changed;
}

model::BinItem MainWindow::dynamicRelinkItem(const QString& binId, const model::BinItem& item) {
    const media::DynamicRelinkSettings s = dynamicRelinkSettings();
    model::BinItem out = item;
    int changed = 0;
    if (item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip) {
        if (s.enabled && dynamicRelinkClip(binId, out, s)) ++changed;
    } else if (item.kind == model::BinItemKind::Sequence && s.enabled) {
        // The clips the sequence's segments use, each once.
        QSet<QString> seen;
        const model::Sequence seq = model::Sequence::fromItem(item);
        for (const model::Track& t : seq.tracks()) {
            for (const model::Segment& seg : t.segments) {
                if (seg.isFiller()) continue;
                const QString bin = seg.binId.isEmpty() ? binId : seg.binId;
                if (seen.contains(bin + QLatin1Char('/') + seg.clipId)) continue;
                seen.insert(bin + QLatin1Char('/') + seg.clipId);
                auto clip = composer_->resolve(bin, seg.clipId);
                if (clip && dynamicRelinkClip(bin, *clip, s)) ++changed;
            }
        }
    }
    if (changed > 0) frames_->clear();
    return out;
}

int MainWindow::dynamicRelinkLoaded() {
    const media::DynamicRelinkSettings s = dynamicRelinkSettings();
    int changed = 0;
    QSet<QString> seen;
    auto relink = [&](const QString& binId, const QString& clipId) {
        if (seen.contains(binId + QLatin1Char('/') + clipId)) return;
        seen.insert(binId + QLatin1Char('/') + clipId);
        auto clip = composer_->resolve(binId, clipId);
        if (clip && dynamicRelinkClip(binId, *clip, s)) ++changed;
    };
    for (MonitorId monitor : {MonitorId::Source, MonitorId::Record}) {
        const model::LoadedClip* loaded = composer_->state(monitor).current();
        if (!loaded) continue;
        if (loaded->item.kind == model::BinItemKind::Sequence) {
            const model::Sequence seq = model::Sequence::fromItem(loaded->item);
            for (const model::Track& t : seq.tracks()) {
                for (const model::Segment& seg : t.segments) {
                    if (!seg.isFiller()) relink(seg.binId.isEmpty() ? loaded->binId : seg.binId, seg.clipId);
                }
            }
        } else if (!loaded->binId.isEmpty()) {
            relink(loaded->binId, loaded->item.id);
        }
    }
    if (changed > 0) {
        frames_->clear();
        ConsoleLog::instance().append(tr("Dynamic Relink: %n clip(s) relinked.", "", changed));
    }
    return changed;
}

// From applySettings(): a changed setting relinks what is loaded (p.1170); the first time only remembers it.
void MainWindow::applyDynamicRelinkSetting() {
    if (!project_) {
        appliedRelink_.reset();
        return;
    }
    const media::DynamicRelinkSettings s = dynamicRelinkSettings();
    const bool changed = appliedRelink_ && !(*appliedRelink_ == s);
    const bool first = !appliedRelink_;
    appliedRelink_ = s;
    timeline_->setDynamicRelinkState(s.enabled, s.overrideWorking);
    if (changed || first) resolutionCache_.clear();
    if (changed || (first && s.enabled)) dynamicRelinkLoaded();
}

model::ClipFacts::Resolution MainWindow::clipResolution(const QString& binId, const QString& clipId, bool target) {
    using R = model::ClipFacts::Resolution;
    const QString key = binId + QLatin1Char('|') + clipId + (target ? QStringLiteral("|t") : QStringLiteral("|w"));
    if (const auto it = resolutionCache_.constFind(key); it != resolutionCache_.constEnd()) return *it;
    R result = R::Matched;
    const auto clip = composer_->resolve(binId, clipId);
    if (clip && project_ &&
        (clip->kind == model::BinItemKind::MasterClip || clip->kind == model::BinItemKind::AudioClip)) {
        const media::DynamicRelinkSettings s = dynamicRelinkSettings();
        const QList<media::MediaCandidate> candidates = media::mediaCandidates(*clip, mediaDb_);
        const auto current = media::currentMedia(*clip, candidates);
        const QSize raster(project_->settings.format.width, project_->settings.format.height);
        const Rational rate = project_->settings.format.editRate;
        if (target) {
            // "Whether media for a clip is available in the target settings ... the same whether the clips are linked to
            // working settings or target settings" (p.1111-1113).
            const media::RelinkChoice choice = media::chooseMedia(candidates, current, s.target, rate, raster, s.mixedRates);
            result = choice.matched ? R::Matched : R::Unavailable;
        } else if (!current) {
            result = R::Offline;
        } else {
            // "Clip coloring for the Show Mismatches display is not used when you select Use Highest Quality or Minimal
            // bandwith as the relink method" (p.1113).
            const media::RelinkRules& rules = s.active();
            const bool unrated = rules.video == media::RelinkRules::Method::HighestQuality ||
                                 rules.video == media::RelinkRules::Method::MinimalBandwidth;
            if (!unrated) {
                const media::RelinkChoice choice = media::chooseMedia(candidates, current, rules, rate, raster, s.mixedRates);
                const bool same = choice.media && choice.media->affinity == current->affinity &&
                                  choice.media->mediaId == current->mediaId;
                result = choice.matched && same ? R::Matched : R::Mismatched;
            }
        }
    }
    resolutionCache_.insert(key, result);
    return result;
}

bool MainWindow::renderAtOtherResolution(const model::EffectRender& render) const {
    if (render.clips.isEmpty()) return false;
    const auto clip = model::binItemFromJson(render.clips.constFirst());
    const auto link = clip ? media::linkOf(*clip) : std::nullopt;
    if (!link || !link->isManaged()) return false;
    const auto set = mediaDb_.set(link->mediaId);
    if (!set || !set->hasVideo()) return false;
    const QString rendered = set->track(QStringLiteral("V1")).resolution;
    return !rendered.isEmpty() && rendered != mediaCreation(QStringLiteral("render")).resolution.name;
}

// The MultiRez button's menu items and the Timeline's questions answered from Dynamic Relink (p.1111-1116).
void MainWindow::connectMultiRez() {
    timeline_->setResolutionResolver([this](const QString& binId, const QString& clipId, bool target) {
        return clipResolution(binId, clipId, target);
    });
    timeline_->setRenderResolutionTest([this](const model::EffectRender& render) { return renderAtOtherResolution(render); });
    // Clip Text > Clip Resolution: the resolution of the media the clip plays now (p.1103-1104).
    timeline_->setClipResolutionText([this](const model::BinItem& clip) {
        if (const auto current = media::currentMedia(clip, media::mediaCandidates(clip, mediaDb_)); current && !current->resolution.isEmpty()) {
            return current->resolution;
        }
        return model::cellText(clip, QStringLiteral("Video"));
    });
    // "In the Show Mismatches display, relinks the clips displayed in the Timeline to the media that matches the working
    // or target settings ... In the Show Target Availability display, refreshes the clip information" (p.1115).
    connect(timeline_, &Timeline::updateMediaStatusRequested, this, [this] {
        loadMediaDatabase();
        resolutionCache_.clear();
        if (timeline_->multiRezDisplay() == Timeline::MultiRezDisplay::Mismatches) dynamicRelinkLoaded();
    });
    connect(timeline_, &Timeline::dynamicRelinkEnableRequested, this, [this](bool on) {
        settings_->setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("enabled"), on);
    });
    connect(timeline_, &Timeline::overrideWorkingRequested, this, [this](bool on) {
        settings_->setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("overrideWorking"), on);
    });
    // "Right-click the MultiRez button at the bottom of the Timeline and select Dynamic Relink Settings" (p.1107).
    connect(timeline_, &Timeline::dynamicRelinkSettingsRequested, this, [this] {
        const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
        const int i = f.activeIndex(QStringLiteral("Dynamic Relink"));
        if (SettingsWindow* w = showSettings(); w && i >= 0) {
            w->showTab(SettingsWindow::Tab::Project);
            w->list(model::SettingsScope::Project)->openEntry(i);
        }
    });
}

} // namespace lumenis::ui
