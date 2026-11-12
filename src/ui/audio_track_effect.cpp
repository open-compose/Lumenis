#include "lumenis/ui/audio_track_effect.h"

#include "lumenis/media/audio_fx.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

// The Audio Track Effect tool (audio_track_effect.h; Editing Guide p.791-795). The effects themselves
// and their parameters are media::audioEffects() (src/media/audio_fx.*); inserts are kept on the
// sequence's tracks (model::Track::inserts) and changed through the Timeline. Each parameter gets a
// row: its name, a slider (log-scaled for frequencies and times) and its value.

namespace lumenis::ui {

namespace {

constexpr int kSteps = 1000;

QToolButton* button(const QString& text, const QString& name, const QString& tip, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setObjectName(name);
    b->setToolTip(tip);
    b->setMinimumWidth(28);
    return b;
}

bool logScale(const media::AudioEffectParam& p) {
    return p.steps == 0 && p.min > 0 && p.max / p.min >= 50 && (p.unit == QLatin1String("Hz") || p.unit == QLatin1String("ms"));
}

int toSlider(const media::AudioEffectParam& p, double value) {
    if (p.steps > 0) return static_cast<int>(std::lround(value - p.min));
    const double v = std::clamp(value, p.min, p.max);
    const double t = logScale(p) ? std::log(v / p.min) / std::log(p.max / p.min) : (v - p.min) / (p.max - p.min);
    return static_cast<int>(std::lround(t * kSteps));
}

double fromSlider(const media::AudioEffectParam& p, int position) {
    if (p.steps > 0) return p.min + position;
    const double t = static_cast<double>(position) / kSteps;
    const double v = logScale(p) ? p.min * std::pow(p.max / p.min, t) : p.min + t * (p.max - p.min);
    // Three significant figures, as the value is shown.
    if (v == 0) return 0;
    const double scale = std::pow(10.0, std::floor(std::log10(std::abs(v))) - 2);
    return std::round(v / scale) * scale;
}

QString slotName(int slot) {
    return QString(QChar(u'a' + slot));
}

} // namespace

AudioTrackEffectTool::AudioTrackEffectTool(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("audioTrackEffect"));
    setAccessibleName(tr("Audio Track Effect")); // screen readers (10.3)
    setFocusPolicy(Qt::StrongFocus);

    // Select Track, Select Insert, Select Effect; Plug-in Settings; Bypass (picture on p.793). Compare
    // and Save Effect come later.
    auto* bar = new QHBoxLayout;
    bar->setSpacing(3);
    trackButton_ = button(QString(), QStringLiteral("selectTrack"), tr("Select Track"), this);
    trackButton_->setPopupMode(QToolButton::InstantPopup);
    trackButton_->setMenu(new QMenu(trackButton_));
    connect(trackButton_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* m = trackButton_->menu();
        m->clear();
        for (const QString& t : audioTracks()) m->addAction(t, this, [this, t] { select(t, slot_); });
    });
    insertButton_ = button(QString(), QStringLiteral("selectInsert"), tr("Select Insert"), this);
    insertButton_->setPopupMode(QToolButton::InstantPopup);
    insertButton_->setMenu(new QMenu(insertButton_));
    connect(insertButton_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* m = insertButton_->menu();
        m->clear();
        for (int i = 0; i < model::kTrackInserts; ++i) {
            const auto in = timeline_.trackInsert(track_, i);
            m->addAction(QStringLiteral("%1: %2").arg(slotName(i), in ? in->name : tr("[Empty]")), this,
                         [this, i] { select(track_, i); });
        }
        m->addSeparator();
        // "Click the Select Insert button and select 'no insert'" removes it (p.795).
        m->addAction(tr("No Insert"), this, [this] { setEffect(QString()); });
    });
    effectButton_ = button(QString(), QStringLiteral("selectEffect"), tr("Select Effect"), this);
    effectButton_->setPopupMode(QToolButton::InstantPopup);
    effectButton_->setMenu(new QMenu(effectButton_));
    connect(effectButton_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* m = effectButton_->menu();
        m->clear();
        QMap<QString, QMenu*> categories;
        for (const media::AudioEffectInfo& info : media::audioEffects()) {
            QMenu*& c = categories[info.category];
            if (!c) c = m->addMenu(info.category);
            c->addAction(info.name, this, [this, id = info.id] { setEffect(id); });
        }
    });
    settingsButton_ = button(QStringLiteral("☰"), QStringLiteral("pluginSettings"), tr("Plug-In Settings"), this);
    settingsButton_->setPopupMode(QToolButton::InstantPopup);
    auto* settings = new QMenu(settingsButton_);
    settings->addAction(tr("Copy Settings"), this, [this] { copySettings(); });
    settings->addAction(tr("Paste Settings"), this, [this] { pasteSettings(); });
    settings->addAction(tr("Reset to Defaults"), this, [this] {
        if (const auto in = insert()) setEffect(in->effect);
    });
    settingsButton_->setMenu(settings);
    bypassButton_ = button(tr("Bypass"), QStringLiteral("bypass"), tr("Bypass (Ctrl+click: all tracks)"), this);
    bypassButton_->setCheckable(true);
    connect(bypassButton_, &QToolButton::clicked, this, [this] {
        bypass(QApplication::keyboardModifiers() & Qt::ControlModifier);
    });
    for (QToolButton* b : {trackButton_, insertButton_, effectButton_, settingsButton_, bypassButton_}) bar->addWidget(b);
    bar->addStretch(1);

    params_ = new QWidget(this);
    params_->setObjectName(QStringLiteral("effectParams"));
    paramsLayout_ = new QVBoxLayout(params_);
    paramsLayout_->setContentsMargins(0, 0, 0, 0);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("trackEffectMessage"));
    message_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(bar);
    layout->addWidget(params_);
    layout->addWidget(message_);
    layout->addStretch(1);

    connect(&timeline_, &Timeline::changed, this, &AudioTrackEffectTool::refresh);
    connect(&timeline_.composer(), &Composer::changed, this, &AudioTrackEffectTool::refresh);
    refresh();
}

QStringList AudioTrackEffectTool::audioTracks() const {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return {};
    QStringList out;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    for (const model::Track& t : seq.tracks()) {
        if (t.kind == model::TrackKind::Audio) out << t.name();
    }
    return out;
}

void AudioTrackEffectTool::select(const QString& track, int slot) {
    track_ = track;
    slot_ = std::clamp(slot, 0, model::kTrackInserts - 1);
    preview_.reset();
    refresh();
}

std::optional<model::TrackInsert> AudioTrackEffectTool::insert() const {
    if (preview_ && preview_->track == track_ && preview_->slot == slot_) return preview_->insert;
    return timeline_.trackInsert(track_, slot_);
}

// Remakes the parameter rows when the effect shown changes, then shows the values.
void AudioTrackEffectTool::refresh() {
    const QStringList tracks = audioTracks();
    if (!tracks.contains(track_)) track_ = tracks.isEmpty() ? QString() : tracks.first();
    trackButton_->setText(track_.isEmpty() ? tr("No track") : track_);
    insertButton_->setText(tr("Insert %1").arg(slotName(slot_)));
    const auto in = insert();
    const media::AudioEffectInfo* info = in ? media::findAudioEffect(in->effect) : nullptr;
    effectButton_->setText(!in ? tr("No Effect") : in->name.isEmpty() ? in->effect : in->name);
    effectButton_->setEnabled(!track_.isEmpty());
    insertButton_->setEnabled(!track_.isEmpty());
    syncing_ = true;
    bypassButton_->setChecked(bypassAll_ || (in && in->bypass));
    syncing_ = false;
    // The button turns blue while bypassed (p.794).
    bypassButton_->setStyleSheet(bypassAll_ || (in && in->bypass) ? QStringLiteral("QToolButton { background: #3c78dc; }") : QString());
    const QString effect = info ? info->id : QString();
    if (effect != shownEffect_) {
        shownEffect_ = effect;
        rebuildParams();
    }
    // "In addition to the 'Unavailable Effect' text, the effect name and other information displays"
    // (p.790).
    if (in && !info) {
        message_->setText(tr("Unavailable Effect: %1 (%2)").arg(in->name, in->effect));
    } else if (message_->text().startsWith(tr("Unavailable Effect"))) {
        message_->clear();
    }
    showValues();
}

// The old rows are taken out of the tool at once but deleted later: a slider of theirs may be what
// changed the effect.
void AudioTrackEffectTool::rebuildParams() {
    auto drop = [](QWidget* w) {
        w->hide();
        w->setParent(nullptr);
        w->deleteLater();
    };
    while (QLayoutItem* item = paramsLayout_->takeAt(0)) {
        if (QWidget* w = item->widget()) drop(w);
        if (QLayout* l = item->layout()) {
            while (QLayoutItem* sub = l->takeAt(0)) {
                if (QWidget* w = sub->widget()) drop(w);
                delete sub;
            }
        }
        delete item;
    }
    const media::AudioEffectInfo* info = media::findAudioEffect(shownEffect_);
    if (!info) return;
    auto* grid = new QGridLayout;
    int row = 0;
    for (const media::AudioEffectParam& p : info->params) {
        auto* name = new QLabel(p.name, params_);
        auto* slider = new QSlider(Qt::Horizontal, params_);
        slider->setObjectName(QStringLiteral("param:") + p.id);
        slider->setRange(0, p.steps > 0 ? p.steps - 1 : kSteps);
        auto* value = new QLabel(params_);
        value->setObjectName(QStringLiteral("value:") + p.id);
        value->setMinimumWidth(70);
        grid->addWidget(name, row, 0);
        grid->addWidget(slider, row, 1);
        grid->addWidget(value, row, 2);
        ++row;
        const media::AudioEffectParam param = p;
        connect(slider, &QSlider::valueChanged, this, [this, slider, param](int v) {
            if (!syncing_) setParam(param.id, fromSlider(param, v), !slider->isSliderDown());
        });
        connect(slider, &QSlider::sliderReleased, this, [this, slider, param] {
            if (!syncing_) setParam(param.id, fromSlider(param, slider->value()), true);
        });
    }
    paramsLayout_->addLayout(grid);
}

void AudioTrackEffectTool::showValues() {
    const auto in = insert();
    const media::AudioEffectInfo* info = in ? media::findAudioEffect(in->effect) : nullptr;
    if (!info) return;
    syncing_ = true;
    for (const media::AudioEffectParam& p : info->params) {
        const double v = in->params.value(p.id, p.defaultValue);
        if (QSlider* s = paramSlider(p.id); s && !s->isSliderDown()) s->setValue(toSlider(p, v));
        if (QLabel* l = paramValue(p.id)) l->setText(media::formatParam(p, v));
    }
    syncing_ = false;
}

QSlider* AudioTrackEffectTool::paramSlider(const QString& id) const {
    return params_->findChild<QSlider*>(QStringLiteral("param:") + id);
}

QLabel* AudioTrackEffectTool::paramValue(const QString& id) const {
    return params_->findChild<QLabel*>(QStringLiteral("value:") + id);
}

bool AudioTrackEffectTool::setEffect(const QString& effectId) {
    message_->clear();
    preview_.reset();
    if (track_.isEmpty()) return false;
    const bool ok = effectId.isEmpty()
                        ? timeline_.setTrackInsert(track_, slot_, std::nullopt, tr("Remove Audio Track Effect"))
                        : timeline_.setTrackInsert(track_, slot_, Timeline::newInsert(effectId), tr("Audio Track Effect"));
    if (!ok) message_->setText(timeline_.lastMessage());
    refresh();
    return ok;
}

bool AudioTrackEffectTool::setParam(const QString& id, double value, bool done) {
    auto in = insert();
    if (!in) return false;
    const media::AudioEffectInfo* info = media::findAudioEffect(in->effect);
    const media::AudioEffectParam* p = info ? info->param(id) : nullptr;
    if (!p) return false;
    in->params.insert(id, std::clamp(value, p->min, p->max));
    if (!done) {
        preview_ = Preview{track_, slot_, *in};
        if (QLabel* l = paramValue(id)) l->setText(media::formatParam(*p, in->params.value(id)));
        emit previewChanged();
        return true;
    }
    const bool had = preview_.has_value();
    preview_.reset();
    const bool ok = timeline_.setTrackInsert(track_, slot_, in, tr("Adjust Audio Track Effect"));
    if (!ok) message_->setText(timeline_.lastMessage());
    if (had) emit previewChanged();
    refresh();
    return ok;
}

void AudioTrackEffectTool::bypass(bool allTracks) {
    if (allTracks) {
        // "If you click Ctrl+Bypass ... you can disable Audio Track effects on all tracks" (p.794).
        bypassAll_ = !bypassAll_;
        emit previewChanged();
        refresh();
        return;
    }
    auto in = timeline_.trackInsert(track_, slot_);
    if (!in) {
        refresh();
        return;
    }
    in->bypass = !in->bypass;
    if (!timeline_.setTrackInsert(track_, slot_, in, in->bypass ? tr("Bypass Audio Track Effect") : tr("Use Audio Track Effect"))) {
        message_->setText(timeline_.lastMessage());
    }
    refresh();
}

void AudioTrackEffectTool::copySettings() {
    clipboard_ = insert();
}

// Only onto the same effect, as the guide says (p.794).
bool AudioTrackEffectTool::pasteSettings() {
    auto in = insert();
    if (!in || !clipboard_ || clipboard_->effect != in->effect) {
        message_->setText(tr("Copy the settings of the same effect first."));
        return false;
    }
    in->params = clipboard_->params;
    const bool ok = timeline_.setTrackInsert(track_, slot_, in, tr("Paste Settings"));
    refresh();
    return ok;
}

void AudioTrackEffectTool::keyPressEvent(QKeyEvent* event) {
    const QStringList tracks = audioTracks();
    const auto at = tracks.indexOf(track_);
    switch (event->key()) {
    case Qt::Key_Up:
        if (at > 0) select(tracks.at(at - 1), slot_);
        return;
    case Qt::Key_Down:
        if (at >= 0 && at + 1 < tracks.size()) select(tracks.at(at + 1), slot_);
        return;
    case Qt::Key_Left:
        select(track_, slot_ - 1);
        return;
    case Qt::Key_Right:
        select(track_, slot_ + 1);
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

} // namespace lumenis::ui
