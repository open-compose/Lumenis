// MainWindow's Dynamic Relink (8.6a; Editing Guide ch.31, p.1102-1110; docs/spec/dynamic-relink.md): the Project
// setting read into media::DynamicRelinkSettings, clips relinked as they load into the Source monitor or the Timeline
// (the Composer's load filter), and the loaded clips relinked again when the setting is applied: "Selecting this option
// and clicking 'Apply' or 'OK' will trigger immediate relink for any media loaded into Source Monitor or the Timeline"
// (p.1170). The choosing itself is media::dynamicRelink.
#include "lumenis/ui/main_window.h"

#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/settings.h"
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
    if (changed) storeClip(binId, clip);
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
    if (changed || (first && s.enabled)) dynamicRelinkLoaded();
}

} // namespace lumenis::ui
