#include "lumenis/ui/audio_suite.h"

#include "lumenis/model/audio_suite.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QDrag>
#include <QDropEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

// Tools > AudioSuite (audio_suite.h; Editing Guide p.796-805; docs/spec/audiosuite.md). The
// processes are src/media/audio_suite.cpp; the effect is set on segments through
// Timeline::setAudioSuite and rendered through Timeline::renderAudioSuite with MainWindow's renderer
// (media::renderAudioSuite); playback plays rendered effects (media::audioPieces). In master clip
// mode MainWindow makes the new clips (media::audioSuiteClip) and their templates
// (model::audioSuiteTemplateItem).

namespace lumenis::ui {

namespace {

constexpr int kSteps = 1000; // a parameter slider's positions

QToolButton* smallButton(const QString& text, const QString& name, const QString& tip, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setObjectName(name);
    b->setToolTip(tip);
    b->setMinimumWidth(22);
    return b;
}

// Sliders as the Audio Track Effect tool's: log-scaled for frequencies and times.
bool logScale(const media::AudioEffectParam& p) {
    return p.steps == 0 && p.min > 0 && p.max / p.min >= 50 && (p.unit == QLatin1String("Hz") || p.unit == QLatin1String("ms"));
}

int toSlider(const media::AudioEffectParam& p, double value) {
    const double v = std::clamp(value, p.min, p.max);
    const double t = logScale(p) ? std::log(v / p.min) / std::log(p.max / p.min) : (v - p.min) / (p.max - p.min);
    return static_cast<int>(std::lround(t * kSteps));
}

double fromSlider(const media::AudioEffectParam& p, int position) {
    const double t = static_cast<double>(position) / kSteps;
    const double v = logScale(p) ? p.min * std::pow(p.max / p.min, t) : p.min + t * (p.max - p.min);
    // Three significant figures, as the value is shown.
    if (v == 0) return 0;
    const double scale = std::pow(10.0, std::floor(std::log10(std::abs(v))) - 2);
    return std::round(v / scale) * scale;
}

} // namespace

// ---------------------------------------------------------------- AudioSuitePluginDialog

AudioSuitePluginDialog::AudioSuitePluginDialog(const media::AudioSuiteInfo& info, const QMap<QString, double>& values,
                                               QWidget* parent)
    : QDialog(parent), info_(info) {
    setObjectName(QStringLiteral("audioSuitePlugin"));
    setWindowTitle(info.name);
    values_ = info.defaults();
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (info.param(it.key())) values_.insert(it.key(), it.value());
    }

    // "The top six buttons are always visible. Buttons unavailable for a plug-in appear dimmed"
    // (p.798).
    auto* top = new QHBoxLayout;
    auto add = [&](const QString& text, const char* name, bool enabled) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QString::fromLatin1(name));
        b->setEnabled(enabled);
        b->setAutoDefault(false);
        if (!enabled) b->setToolTip(tr("Not available yet"));
        top->addWidget(b);
        return b;
    };
    QPushButton* ok = add(tr("OK"), "ok", true);
    QPushButton* cancel = add(tr("Cancel"), "cancel", true);
    add(tr("Preview"), "preview", false);
    QPushButton* render = add(tr("Render"), "render", true);
    add(tr("Bypass"), "bypass", false);
    add(tr("Find Level"), "findLevel", false);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(render, &QPushButton::clicked, this, [this] {
        render_ = true;
        accept();
    });
    auto* second = new QHBoxLayout;
    for (const auto& [text, name] : {std::pair{tr("Plug-In Settings"), "settings"}, std::pair{tr("Compare"), "compare"}}) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QString::fromLatin1(name));
        b->setEnabled(false);
        b->setToolTip(tr("Not available yet"));
        second->addWidget(b);
    }
    second->addStretch(1);

    auto* form = new QFormLayout;
    for (const media::AudioEffectParam& p : info.params) {
        auto* value = new QLabel(media::formatParam(p, values_.value(p.id)), this);
        value->setObjectName(QStringLiteral("value:") + p.id);
        value->setMinimumWidth(70);
        auto* row = new QHBoxLayout;
        if (p.steps > 0) {
            auto* box = new QComboBox(this);
            box->setObjectName(QStringLiteral("choice:") + p.id);
            box->addItems(p.choices);
            box->setCurrentIndex(static_cast<int>(std::lround(values_.value(p.id) - p.min)));
            connect(box, &QComboBox::currentIndexChanged, this, [this, p](int i) { setValue(p.id, p.min + i); });
            row->addWidget(box, 1);
            value->hide();
        } else {
            auto* slider = new QSlider(Qt::Horizontal, this);
            slider->setObjectName(QStringLiteral("param:") + p.id);
            slider->setRange(0, kSteps);
            slider->setValue(toSlider(p, values_.value(p.id)));
            slider->setMinimumWidth(180);
            connect(slider, &QSlider::valueChanged, this, [this, p](int v) { setValue(p.id, fromSlider(p, v)); });
            row->addWidget(slider, 1);
        }
        row->addWidget(value);
        form->addRow(p.name, row);
    }
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(second);
    if (info.params.isEmpty()) {
        auto* none = new QLabel(tr("%1 has no settings.").arg(info.name), this);
        none->setObjectName(QStringLiteral("noSettings"));
        layout->addWidget(none);
    }
    layout->addLayout(form);
}

void AudioSuitePluginDialog::setValue(const QString& id, double value) {
    const media::AudioEffectParam* p = info_.param(id);
    if (!p) return;
    const double v = std::clamp(value, p->min, p->max);
    values_.insert(id, v);
    if (QLabel* l = paramValue(id)) l->setText(media::formatParam(*p, v));
    if (QSlider* s = paramSlider(id); s && fromSlider(*p, s->value()) != v) {
        const QSignalBlocker block(s);
        s->setValue(toSlider(*p, v));
    }
    if (QComboBox* c = paramChoice(id)) {
        const QSignalBlocker block(c);
        c->setCurrentIndex(static_cast<int>(std::lround(v - p->min)));
    }
}

QPushButton* AudioSuitePluginDialog::button(const QString& name) const {
    return findChild<QPushButton*>(name);
}

QSlider* AudioSuitePluginDialog::paramSlider(const QString& id) const {
    return findChild<QSlider*>(QStringLiteral("param:") + id);
}

QComboBox* AudioSuitePluginDialog::paramChoice(const QString& id) const {
    return findChild<QComboBox*>(QStringLiteral("choice:") + id);
}

QLabel* AudioSuitePluginDialog::paramValue(const QString& id) const {
    return findChild<QLabel*>(QStringLiteral("value:") + id);
}

// ---------------------------------------------------------------- AudioSuiteTool

namespace {

// The audio tracks a clip has: its Tracks column's, else its media's channels.
int audioTrackCount(const model::BinItem& item) {
    int n = 0;
    for (const QString& t : model::parseTrackList(item.fields.value(QStringLiteral("Tracks")))) {
        if (const auto ref = model::parseTrackName(t); ref && ref->kind == model::TrackKind::Audio) n = std::max(n, ref->number);
    }
    return n;
}

// Media Composer names new clips and templates one way on Windows, another on macOS (p.803-804).
constexpr bool kMacNames =
#ifdef Q_OS_MACOS
    true;
#else
    false;
#endif

} // namespace

AudioSuiteTool::AudioSuiteTool(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("audioSuite"));
    setAcceptDrops(true);

    // The default layout (picture and table on p.797), left to right: the effect icon, Audio Loop
    // Play, Render Effect, the Fast menu, the Track Selection Menu, Nested Level Selection, Activate
    // Current Plug-In; below, the status display, the Plug-In Selection menu and the Target Drive
    // menu. Toggle Master Clip Mode and Display/Hide Master Clip Controls are at the right (p.801).
    auto* bar = new QHBoxLayout;
    bar->setSpacing(3);
    // The effect icon: dragged to a bin, it saves the effect as a template (p.798).
    effectIcon_ = smallButton(tr("AS"), QStringLiteral("effectIcon"), tr("Effect icon: drag to a bin for a template"), this);
    effectIcon_->installEventFilter(this);
    bar->addWidget(effectIcon_);
    bar->addWidget(timeline_.makeAudioLoopPlayButton(this));
    renderButton_ = smallButton(tr("Render"), QStringLiteral("renderEffect"), tr("Render Effect"), this);
    connect(renderButton_, &QToolButton::clicked, this, [this] { render(); });
    bar->addWidget(renderButton_);
    fast_ = smallButton(QStringLiteral("☰"), QStringLiteral("fastMenu"), tr("Fast Menu"), this);
    fast_->setPopupMode(QToolButton::InstantPopup);
    fast_->setMenu(new QMenu(fast_));
    connect(fast_->menu(), &QMenu::aboutToShow, this, [this] { fastMenu(); });
    bar->addWidget(fast_);
    trackButton_ = smallButton(QString(), QStringLiteral("trackSelection"), tr("Track Selection Menu"), this);
    trackButton_->setPopupMode(QToolButton::InstantPopup);
    trackButton_->setMenu(new QMenu(trackButton_));
    // "When you select an item from this menu, the system selects or deselects the corresponding
    // track in the Timeline"; Shift adds tracks, marked with plus signs (p.797).
    connect(trackButton_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* menu = trackButton_->menu();
        menu->clear();
        const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
        if (!rec || rec->item.kind != model::BinItemKind::Sequence) return;
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        const QStringList selected = tracks();
        for (const model::Track& t : seq.tracks()) {
            if (t.kind != model::TrackKind::Audio) continue;
            const QString name = t.name();
            const bool on = selected.contains(name);
            QAction* a = menu->addAction((on && selected.size() > 1 ? QStringLiteral("+ ") : QString()) + name, this, [this, name] {
                chooseTrack(name, QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
            });
            a->setCheckable(true);
            a->setChecked(on);
        }
    });
    bar->addWidget(trackButton_);
    // The Nested Level Selection menu (6 on p.797, p.800).
    levelButton_ = smallButton(tr("1"), QStringLiteral("nestedLevel"), tr("Nested Level Selection Menu"), this);
    levelButton_->setPopupMode(QToolButton::InstantPopup);
    levelButton_->setMenu(new QMenu(levelButton_));
    connect(levelButton_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* menu = levelButton_->menu();
        menu->clear();
        const QStringList items = levelItems();
        for (int i = 0; i < items.size(); ++i) {
            QAction* a = menu->addAction(items.at(i), this, [this, i] { chooseLevel(i); });
            a->setCheckable(true);
            a->setChecked(i == level_);
        }
    });
    bar->addWidget(levelButton_);
    activateButton_ = smallButton(tr("Activate"), QStringLiteral("activatePlugin"), tr("Activate Current Plug-In"), this);
    connect(activateButton_, &QToolButton::clicked, this, [this] { activate(); });
    bar->addWidget(activateButton_);
    bar->addStretch(1);
    masterButton_ = smallButton(tr("Clip"), QStringLiteral("masterClipMode"), tr("Toggle Master Clip Mode"), this);
    masterButton_->setCheckable(true);
    connect(masterButton_, &QToolButton::clicked, this, [this](bool on) { setMasterClipMode(on); });
    bar->addWidget(masterButton_);
    controlsButton_ = smallButton(QStringLiteral("▾"), QStringLiteral("masterClipControls"), tr("Display/Hide Master Clip Controls"), this);
    controlsButton_->setCheckable(true);
    connect(controlsButton_, &QToolButton::clicked, this, [this](bool on) { showControls(on); });
    bar->addWidget(controlsButton_);

    auto* bottom = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("status"));
    status_->setFrameShape(QFrame::StyledPanel);
    status_->setMinimumWidth(160);
    bottom->addWidget(status_, 1);
    pluginButton_ = smallButton(tr("None"), QStringLiteral("pluginSelection"), tr("Plug-In Selection"), this);
    pluginButton_->setPopupMode(QToolButton::InstantPopup);
    pluginButton_->setMenu(new QMenu(pluginButton_));
    // The plug-ins by category, then None (ours). Those only for new master clips show only in
    // master clip mode.
    connect(pluginButton_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* menu = pluginButton_->menu();
        menu->clear();
        // EQ first, as the picture on p.800 lists it, on the Timeline only.
        if (!masterMode_) {
            QAction* eq = menu->addAction(tr("EQ"), this, [this] { choosePlugin(QString::fromLatin1(kEqPlugin)); });
            eq->setCheckable(true);
            eq->setChecked(plugin_ == QLatin1String(kEqPlugin));
        }
        QMap<QString, QMenu*> groups;
        for (const media::AudioSuiteInfo& i : media::audioSuitePlugins()) {
            if (i.masterClipOnly && !masterMode_) continue;
            QMenu*& group = groups[i.category];
            if (!group) group = menu->addMenu(i.category);
            const QString id = i.id;
            QAction* a = group->addAction(i.name, this, [this, id] { choosePlugin(id); });
            a->setCheckable(true);
            a->setChecked(id == plugin_);
        }
        menu->addSeparator();
        menu->addAction(tr("None"), this, [this] { choosePlugin(QString()); });
    });
    bottom->addWidget(pluginButton_);
    drive_ = new QComboBox(this);
    drive_->setObjectName(QStringLiteral("targetDrive"));
    drive_->setToolTip(tr("Target Drive"));
    bottom->addWidget(drive_);

    // The master clip controls (picture and table on p.801-802), hidden until shown.
    controls_ = new QWidget(this);
    controls_->setObjectName(QStringLiteral("masterControls"));
    auto* rows = new QVBoxLayout(controls_);
    rows->setContentsMargins(0, 0, 0, 0);
    auto* first = new QHBoxLayout;
    clipMenu_ = new QComboBox(controls_);
    clipMenu_->setObjectName(QStringLiteral("clipSelection"));
    clipMenu_->setToolTip(tr("Clip Selection"));
    clipMenu_->setMinimumWidth(140);
    connect(clipMenu_, &QComboBox::activated, this, [this](int i) { setActiveClip(i); });
    first->addWidget(clipMenu_, 1);
    loadSource_ = smallButton(tr("Source"), QStringLiteral("loadInSource"), tr("Load In Source Monitor"), controls_);
    connect(loadSource_, &QToolButton::clicked, this, [this] { loadInSourceMonitor(); });
    first->addWidget(loadSource_);
    markIn_ = new QLabel(tr("IN"), controls_);
    markIn_->setObjectName(QStringLiteral("markInIndicator"));
    markOut_ = new QLabel(tr("OUT"), controls_);
    markOut_->setObjectName(QStringLiteral("markOutIndicator"));
    for (QLabel* l : {markIn_, markOut_}) {
        l->setFrameShape(QFrame::StyledPanel);
        l->setAlignment(Qt::AlignCenter);
        l->setMinimumWidth(30);
        l->setAutoFillBackground(true);
        first->addWidget(l);
    }
    findSource_ = smallButton(tr("Find Source"), QStringLiteral("findSource"), tr("Find Source From Effect"), controls_);
    connect(findSource_, &QToolButton::clicked, this, [this] { findSourceFromEffect(); });
    first->addWidget(findSource_);
    rows->addLayout(first);
    inputRow_ = new QWidget(controls_);
    inputRow_->setObjectName(QStringLiteral("inputTracks"));
    auto* inputs = new QHBoxLayout(inputRow_);
    inputs->setContentsMargins(0, 0, 0, 0);
    inputs->setSpacing(2);
    inputs->addStretch(1);
    rows->addWidget(inputRow_);
    auto* third = new QHBoxLayout;
    mode_ = new QComboBox(controls_);
    mode_->setObjectName(QStringLiteral("processingMode"));
    mode_->setToolTip(tr("Processing Mode Selection"));
    connect(mode_, &QComboBox::activated, this, [this](int i) { setProcessingMode(i); });
    third->addWidget(mode_, 1);
    bin_ = new QComboBox(controls_);
    bin_->setObjectName(QStringLiteral("targetBin"));
    bin_->setToolTip(tr("Target Bin for New Master Clip"));
    third->addWidget(bin_, 1);
    rows->addLayout(third);
    auto* fourth = new QHBoxLayout;
    loadResult_ = new QCheckBox(tr("Load Result"), controls_);
    loadResult_->setObjectName(QStringLiteral("loadResult"));
    fourth->addWidget(loadResult_);
    fourth->addStretch(1);
    fourth->addWidget(new QLabel(tr("Handle (sec)"), controls_));
    handle_ = new QLineEdit(QStringLiteral("0"), controls_);
    handle_->setObjectName(QStringLiteral("handleLength"));
    handle_->setToolTip(tr("Handle Length for End of Master Clip (seconds)"));
    handle_->setValidator(new QDoubleValidator(0, 3600, 2, handle_));
    handle_->setFixedWidth(60);
    fourth->addWidget(handle_);
    rows->addLayout(fourth);
    controls_->hide();

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("message"));
    message_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addLayout(bottom);
    layout->addWidget(controls_);
    layout->addWidget(message_);
    layout->addStretch(1);

    connect(&timeline_, &Timeline::changed, this, &AudioSuiteTool::refresh);
    connect(&timeline_.composer(), &Composer::changed, this, &AudioSuiteTool::refresh);
    refresh();
}

QStringList AudioSuiteTool::tracks() const {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return {};
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    QStringList out;
    for (const QString& t : timeline_.patch().selectedRecordTracks(seq)) {
        const model::Track* track = seq.track(t);
        if (track && track->kind == model::TrackKind::Audio) out << t;
    }
    return out;
}

QString AudioSuiteTool::track() const {
    const QStringList t = tracks();
    return t.isEmpty() ? QString() : t.first();
}

void AudioSuiteTool::chooseTrack(const QString& name, bool add) {
    if (add) {
        timeline_.toggleRecordTrack(name);
        return;
    }
    // The track alone: the other selected audio tracks are deselected.
    for (const QString& t : tracks()) {
        if (t != name) timeline_.toggleRecordTrack(t);
    }
    if (!tracks().contains(name)) timeline_.toggleRecordTrack(name);
}

std::optional<model::AudioSuiteEffect> AudioSuiteTool::current() const {
    const QString t = track();
    return t.isEmpty() || level_ < 1 ? std::nullopt : timeline_.audioSuiteAt(t, level_);
}

void AudioSuiteTool::chooseLevel(int level) {
    level_ = std::clamp(level, 0, std::max(0, static_cast<int>(levelItems().size()) - 1));
    // An empty level shows no plug-in until one is chosen.
    if (level_ == 0) {
        const QString t = track();
        const auto s = t.isEmpty() ? std::nullopt : timeline_.audioSegmentAt(t);
        plugin_ = s && s->eq ? QString::fromLatin1(kEqPlugin) : QString();
    } else if (!current()) {
        plugin_.clear();
    }
    refresh();
}

QStringList AudioSuiteTool::levelItems() const {
    const QString t = track();
    const auto s = t.isEmpty() ? std::nullopt : timeline_.audioSegmentAt(t);
    QStringList items{s && s->eq ? tr("0 - EQ") : tr("0")};
    const QList<model::AudioSuiteEffect> levels = s ? model::audioSuiteLevels(*s) : QList<model::AudioSuiteEffect>{};
    for (int i = 0; i < levels.size(); ++i) items << tr("%1 - %2").arg(i + 1).arg(levels.at(i).name);
    items << QString::number(levels.size() + 1);
    return items;
}

bool AudioSuiteTool::fail(const QString& text) {
    message_->setText(text);
    return false;
}

bool AudioSuiteTool::choosePlugin(const QString& id) {
    message_->clear();
    if (masterMode_) {
        if (id.isEmpty()) {
            plugin_.clear();
            refresh();
            return true;
        }
        const auto info = media::findAudioSuitePlugin(id);
        if (!info) return fail(tr("The Audio Processing plug-in %1 is not installed.").arg(id));
        plugin_ = id;
        lastValues_ = info->defaults();
        refresh();
        return true;
    }
    if (tracks().isEmpty()) return fail(tr("Select an audio track."));
    if (id == QLatin1String(kEqPlugin)) {
        // "The real-time EQ effect is only allowed on the first nested level (level 0)" (p.800): a flat EQ, which the
        // Audio EQ tool then sets.
        level_ = 0;
        plugin_ = id;
        const QString t = track();
        const auto s = t.isEmpty() ? std::nullopt : timeline_.audioSegmentAt(t);
        if (!(s && s->eq) && !timeline_.setEq(tracks(), model::AudioEq{}, false, tr("Audio EQ"))) return fail(timeline_.lastMessage());
        refresh();
        return true;
    }
    if (id.isEmpty()) {
        if (level_ == 0) {
            plugin_.clear();
            if (!timeline_.setEq(tracks(), std::nullopt, false, tr("Remove EQ"))) return fail(timeline_.lastMessage());
            refresh();
            return true;
        }
        if (!timeline_.setAudioSuite(tracks(), std::nullopt, false, tr("Remove Audio Processing"), level_)) return fail(timeline_.lastMessage());
        refresh();
        return true;
    }
    // Level 0 holds the EQ alone: an AudioSuite plug-in chosen there goes to level 1 (ours).
    if (level_ == 0) level_ = 1;
    const auto info = media::findAudioSuitePlugin(id);
    if (!info) return fail(tr("The Audio Processing plug-in %1 is not installed.").arg(id));
    // "This plug-in applies only when you use the Create New Master Clips features" (p.839).
    if (info->masterClipOnly) return fail(tr("%1 works only on master clips (Toggle Master Clip Mode).").arg(info->name));
    model::AudioSuiteEffect e;
    e.effect = id;
    e.name = info->name;
    e.params = info->defaults();
    plugin_ = id;
    lastValues_ = e.params;
    if (!timeline_.setAudioSuite(tracks(), e, false, tr("Audio Processing %1").arg(info->name), std::max(level_, 1))) {
        return fail(timeline_.lastMessage());
    }
    refresh();
    return true;
}

bool AudioSuiteTool::setValues(const QMap<QString, double>& values) {
    message_->clear();
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (!info) return fail(tr("Choose a plug-in first."));
    model::AudioSuiteEffect e;
    e.effect = plugin_;
    e.name = info->name;
    e.params = info->defaults();
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (info->param(it.key())) e.params.insert(it.key(), it.value());
    }
    lastValues_ = e.params;
    if (masterMode_) {
        refresh();
        return true;
    }
    if (!timeline_.setAudioSuite(tracks(), e, false, tr("Audio Processing %1").arg(info->name), std::max(level_, 1))) {
        return fail(timeline_.lastMessage());
    }
    return true;
}

AudioSuitePluginDialog* AudioSuiteTool::activate() {
    message_->clear();
    if (!masterMode_ && plugin_ == QLatin1String(kEqPlugin)) {
        emit eqToolRequested();
        return nullptr;
    }
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (!info) {
        fail(tr("Choose a plug-in first."));
        return nullptr;
    }
    const auto shown = masterMode_ ? std::nullopt : current();
    const QMap<QString, double> values = shown && shown->effect == plugin_ ? shown->params : lastValues_;
    auto* dialog = new AudioSuitePluginDialog(*info, values, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        // OK saves the effect; Render saves and renders it (p.798).
        if (setValues(dialog->values()) && dialog->renderRequested()) render();
    });
    dialog->open();
    return dialog;
}

bool AudioSuiteTool::renderRange(bool scoped) {
    message_->clear();
    if (!renderer_) return fail(tr("Audio Processing effects cannot be rendered here."));
    const QStringList list = tracks();
    if (list.isEmpty()) return fail(tr("Select an audio track."));
    const QString target = drive();
    const Timeline::AudioSuiteRenderer render = [this, target](const model::Segment& s, QString* error) {
        return renderer_(s, target, error);
    };
    // The segment under the position, or the marks (without an OUT, to the end of every track): one
    // undo step for all the tracks.
    const model::MonitorState& record = timeline_.composer().state(MonitorId::Record);
    std::int64_t from = record.position();
    std::int64_t to = from + 1;
    if (scoped) {
        from = record.in().value_or(0);
        to = record.out() ? *record.out() + 1 : std::numeric_limits<std::int64_t>::max();
    }
    if (timeline_.audioSuiteToRender(list, from, to) == 0) return fail(tr("There is no Audio Processing effect to render."));
    const auto done = timeline_.renderAudioSuite(list, from, to, render);
    if (!done) return fail(timeline_.lastMessage());
    message_->setText(tr("%n effect(s) rendered.", "", *done));
    return true;
}

bool AudioSuiteTool::renderMasterClip() {
    message_->clear();
    if (!clipMaker_) return fail(tr("New master clips cannot be made here."));
    if (active_ < 0) return fail(tr("Drag a master clip into the Audio Processing window."));
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (!info) return fail(tr("Choose a plug-in first."));
    if (inputs_.isEmpty()) return fail(tr("Select the input tracks."));
    if (targetBin().isEmpty()) return fail(tr("Choose a target bin."));
    MasterClipRender r;
    r.source = clips_.at(active_);
    r.tracks = inputs_;
    r.in = clipIn();
    r.out = clipOut();
    r.handleSeconds = handleSeconds();
    r.effect.effect = plugin_;
    r.effect.name = info->name;
    r.effect.params = info->defaults();
    for (auto it = lastValues_.cbegin(); it != lastValues_.cend(); ++it) {
        if (info->param(it.key())) r.effect.params.insert(it.key(), it.value());
    }
    r.targetBin = targetBin();
    r.drive = drive();
    r.loadResult = loadResult();
    QString error;
    if (!clipMaker_(r, &error)) return fail(error.isEmpty() ? tr("The new master clip could not be made.") : error);
    message_->setText(tr("%1 made.").arg(model::audioSuiteClipName(r.source.item.name, info->name, kMacNames)));
    return true;
}

bool AudioSuiteTool::render() {
    return masterMode_ ? renderMasterClip() : renderRange(false);
}

QString AudioSuiteTool::scope() const {
    const model::MonitorState& r = timeline_.composer().state(MonitorId::Record);
    if (r.out()) return tr("IN/OUT");
    if (r.in()) return tr("From IN");
    return tr("Global");
}

bool AudioSuiteTool::setInScope() {
    message_->clear();
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (!info) return fail(tr("Choose a plug-in first."));
    model::AudioSuiteEffect e;
    e.effect = plugin_;
    e.name = info->name;
    e.params = info->defaults();
    for (auto it = lastValues_.cbegin(); it != lastValues_.cend(); ++it) {
        if (info->param(it.key())) e.params.insert(it.key(), it.value());
    }
    if (!timeline_.setAudioSuite(tracks(), e, true, tr("Set Audio Processing - %1").arg(scope()), std::max(level_, 1))) {
        return fail(timeline_.lastMessage());
    }
    return true;
}

bool AudioSuiteTool::renderInScope() {
    return renderRange(true);
}

bool AudioSuiteTool::removeInScope() {
    message_->clear();
    if (!timeline_.setAudioSuite(tracks(), std::nullopt, true, tr("Remove Audio Processing - %1").arg(scope()), std::max(level_, 1))) {
        return fail(timeline_.lastMessage());
    }
    return true;
}

bool AudioSuiteTool::applyTemplate(const model::AudioSuiteTemplate& t) {
    message_->clear();
    const auto info = media::findAudioSuitePlugin(t.effect.effect);
    if (!info) return fail(tr("The Audio Processing plug-in %1 is not installed.").arg(t.effect.name));
    if (masterMode_) {
        plugin_ = t.effect.effect;
        lastValues_ = info->defaults();
        for (auto it = t.effect.params.cbegin(); it != t.effect.params.cend(); ++it) lastValues_.insert(it.key(), it.value());
        refresh();
        return true;
    }
    if (info->masterClipOnly) return fail(tr("%1 works only on master clips (Toggle Master Clip Mode).").arg(info->name));
    if (tracks().isEmpty()) return fail(tr("Select an audio track."));
    model::AudioSuiteEffect e = t.effect;
    e.name = info->name;
    plugin_ = e.effect;
    lastValues_ = e.params;
    // One undo step, as dropping a template is.
    if (!timeline_.setAudioSuite(tracks(), e, false, tr("Audio Processing %1").arg(info->name), std::max(level_, 1))) {
        return fail(timeline_.lastMessage());
    }
    refresh();
    return true;
}

// ---- master clip processing

void AudioSuiteTool::setMasterClipMode(bool on) {
    masterMode_ = on;
    masterButton_->setChecked(on);
    // "The button displays as yellow when master clip processing mode is active" (p.802).
    masterButton_->setStyleSheet(on ? QStringLiteral("QToolButton { background-color: #e6c800; color: black; }") : QString());
    if (!on && !plugin_.isEmpty()) {
        if (const auto info = media::findAudioSuitePlugin(plugin_); info && info->masterClipOnly) plugin_.clear();
    }
    // The real-time EQ is a Timeline effect: new master clips take AudioSuite plug-ins only.
    if (on && plugin_ == QLatin1String(kEqPlugin)) plugin_.clear();
    refresh();
}

bool AudioSuiteTool::controlsShown() const {
    return !controls_->isHidden();
}

void AudioSuiteTool::showControls(bool on) {
    controls_->setVisible(on);
    controlsButton_->setChecked(on);
}

int AudioSuiteTool::addItems(const QString& binId, const QList<model::BinItem>& items) {
    message_->clear();
    int taken = 0;
    for (const model::BinItem& item : items) {
        if (const auto t = model::audioSuiteTemplateOf(item)) {
            // "When you drop an AudioSuite effect template into the AudioSuite window, the system
            // activates this button" (p.802).
            pendingTemplate_ = t;
            ++taken;
            continue;
        }
        const bool clip = item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::Subclip ||
                          item.kind == model::BinItemKind::AudioClip;
        if (!clip || audioTrackCount(item) == 0) continue;
        int at = -1;
        for (int i = 0; i < clips_.size(); ++i) {
            if (clips_.at(i).item.id == item.id) at = i;
        }
        if (at < 0) {
            clips_.append({binId, item});
            at = static_cast<int>(clips_.size()) - 1;
        } else {
            clips_[at] = {binId, item};
        }
        active_ = at;
        ++taken;
    }
    if (taken == 0) {
        fail(tr("Only master clips and subclips with sound, and Audio Processing templates, can be dropped here."));
        return 0;
    }
    // "Media Composer automatically enters Master Clip Processing mode and expands the AudioSuite
    // window" (p.803).
    if (active_ >= 0) {
        showClip();
        setMasterClipMode(true);
    }
    showControls(true);
    refresh();
    return taken;
}

void AudioSuiteTool::setActiveClip(int index) {
    if (index < 0 || index >= clips_.size()) return;
    active_ = index;
    showClip();
    refresh();
}

void AudioSuiteTool::showClip() {
    {
        const QSignalBlocker block(clipMenu_);
        clipMenu_->clear();
        for (const SourceClip& c : clips_) clipMenu_->addItem(c.item.name);
        clipMenu_->setCurrentIndex(active_);
    }
    inputs_.clear();
    if (active_ >= 0) {
        for (int t = 0; t < audioTrackCount(clips_.at(active_).item); ++t) inputs_ << t;
    }
    rebuildInputs();
}

void AudioSuiteTool::rebuildInputs() {
    for (QToolButton* b : inputRow_->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
        b->hide();
        b->setParent(nullptr);
        b->deleteLater();
    }
    if (active_ < 0) return;
    auto* row = static_cast<QHBoxLayout*>(inputRow_->layout());
    const int n = audioTrackCount(clips_.at(active_).item);
    for (int t = 0; t < n; ++t) {
        auto* b = smallButton(QStringLiteral("A%1").arg(t + 1), QStringLiteral("input:A%1").arg(t + 1), tr("Input Source Track"), inputRow_);
        b->setCheckable(true);
        b->setChecked(inputs_.contains(t));
        connect(b, &QToolButton::clicked, this, [this, t](bool on) { setInputTrack(t, on); });
        row->insertWidget(t, b);
    }
}

QToolButton* AudioSuiteTool::inputButton(int track) const {
    return inputRow_->findChild<QToolButton*>(QStringLiteral("input:A%1").arg(track + 1));
}

QList<int> AudioSuiteTool::inputTracks() const {
    return inputs_;
}

void AudioSuiteTool::setInputTrack(int track, bool on) {
    if (active_ < 0 || track < 0 || track >= audioTrackCount(clips_.at(active_).item)) return;
    if (on && !inputs_.contains(track)) inputs_.append(track);
    if (!on) inputs_.removeAll(track);
    std::sort(inputs_.begin(), inputs_.end());
    if (QToolButton* b = inputButton(track)) b->setChecked(on);
    refresh();
}

QStringList AudioSuiteTool::processingModes() const {
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (info) {
        if (const media::AudioEffectParam* p = info->param(QStringLiteral("mode")); p && p->steps > 0) return p->choices;
    }
    return {tr("Mono")};
}

int AudioSuiteTool::processingMode() const {
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (!info || !info->param(QStringLiteral("mode"))) return 0;
    return static_cast<int>(std::lround(lastValues_.value(QStringLiteral("mode"))));
}

void AudioSuiteTool::setProcessingMode(int index) {
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (!info || !info->param(QStringLiteral("mode"))) return;
    QMap<QString, double> values = lastValues_;
    values.insert(QStringLiteral("mode"), index);
    setValues(values);
}

std::optional<std::int64_t> AudioSuiteTool::clipIn() const {
    if (active_ < 0) return std::nullopt;
    const model::LoadedClip* c = timeline_.composer().state(MonitorId::Source).loaded(clips_.at(active_).item.id);
    if (c && (c->in || c->out)) return c->in;
    // A template's marks, for its clip found with Find Source From Effect.
    if (pendingTemplate_ && pendingTemplate_->sourceClip == clips_.at(active_).item.id) return pendingTemplate_->in;
    return std::nullopt;
}

std::optional<std::int64_t> AudioSuiteTool::clipOut() const {
    if (active_ < 0) return std::nullopt;
    const model::LoadedClip* c = timeline_.composer().state(MonitorId::Source).loaded(clips_.at(active_).item.id);
    if (c && (c->in || c->out)) return c->out;
    if (pendingTemplate_ && pendingTemplate_->sourceClip == clips_.at(active_).item.id) return pendingTemplate_->out;
    return std::nullopt;
}

double AudioSuiteTool::handleSeconds() const {
    return std::max(QLocale().toDouble(handle_->text()), 0.0);
}

void AudioSuiteTool::setHandleSeconds(double seconds) {
    handle_->setText(QLocale().toString(std::max(seconds, 0.0), 'g', 6));
}

bool AudioSuiteTool::loadResult() const {
    return loadResult_->isChecked();
}

void AudioSuiteTool::setLoadResult(bool on) {
    loadResult_->setChecked(on);
}

void AudioSuiteTool::setBins(const QList<QPair<QString, QString>>& bins, const QString& current) {
    const QString was = targetBin().isEmpty() ? current : targetBin();
    const QSignalBlocker block(bin_);
    bin_->clear();
    for (const auto& [id, name] : bins) bin_->addItem(name, id);
    const int i = bin_->findData(was);
    bin_->setCurrentIndex(i >= 0 ? i : 0);
}

QString AudioSuiteTool::targetBin() const {
    return bin_->currentData().toString();
}

bool AudioSuiteTool::loadInSourceMonitor() {
    if (active_ < 0) return fail(tr("Drag a master clip into the Audio Processing window."));
    emit sourceRequested(clips_.at(active_).binId, clips_.at(active_).item);
    return true;
}

bool AudioSuiteTool::findSourceFromEffect() {
    message_->clear();
    if (!pendingTemplate_) return fail(tr("Drag an Audio Processing template into the Audio Processing window."));
    const model::AudioSuiteTemplate t = *pendingTemplate_;
    // "A template in the AudioSuite window must reference an existing master clip" (p.802).
    const std::optional<model::BinItem> clip = t.hasSource() && resolver_ ? resolver_(t.sourceBin, t.sourceClip) : std::nullopt;
    if (!clip) return fail(tr("The template's master clip cannot be found."));
    addItems(t.sourceBin, {*clip});
    // The clip with the template's plug-in values, tracks and handle (p.804).
    QList<int> chosen;
    for (const int k : t.tracks) {
        if (k >= 0 && k < audioTrackCount(*clip)) chosen << k;
    }
    if (!chosen.isEmpty()) {
        inputs_ = chosen;
        rebuildInputs();
    }
    setHandleSeconds(t.handleSeconds);
    applyTemplate(t);
    return true;
}

std::optional<model::AudioSuiteTemplate> AudioSuiteTool::iconTemplate() const {
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (!info) return std::nullopt;
    model::AudioSuiteTemplate t;
    t.effect.effect = plugin_;
    t.effect.name = info->name;
    t.effect.params = info->defaults();
    for (auto it = lastValues_.cbegin(); it != lastValues_.cend(); ++it) {
        if (info->param(it.key())) t.effect.params.insert(it.key(), it.value());
    }
    if (masterMode_ && active_ >= 0) {
        t.sourceBin = clips_.at(active_).binId;
        t.sourceClip = clips_.at(active_).item.id;
        t.tracks = inputs_;
        t.in = clipIn();
        t.out = clipOut();
        t.handleSeconds = handleSeconds();
    }
    return t;
}

void AudioSuiteTool::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(binItemsMime())) event->acceptProposedAction();
}

void AudioSuiteTool::dropEvent(QDropEvent* event) {
    QStringList lines = QString::fromUtf8(event->mimeData()->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2 || !resolver_) return;
    const QString binId = lines.takeFirst();
    QList<model::BinItem> items;
    for (const QString& id : lines) {
        if (const auto item = resolver_(binId, id)) items << *item;
    }
    if (addItems(binId, items) > 0) event->acceptProposedAction();
}

// The effect icon drags a template to a bin (p.798).
bool AudioSuiteTool::eventFilter(QObject* watched, QEvent* event) {
    if (watched == effectIcon_) {
        if (event->type() == QEvent::MouseButtonPress) {
            pressAt_ = static_cast<QMouseEvent*>(event)->position().toPoint();
        } else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->buttons().testFlag(Qt::LeftButton) &&
                (me->position().toPoint() - pressAt_).manhattanLength() >= QApplication::startDragDistance()) {
                const auto t = iconTemplate();
                if (!t) return true;
                const QString name = t->hasSource() ? model::audioSuiteTemplateName(clips_.at(active_).item.name, t->effect.name, kMacNames)
                                                    : t->effect.name;
                auto* mime = new QMimeData;
                mime->setData(audioSuiteTemplateMime(),
                              QJsonDocument(QJsonObject{{QStringLiteral("name"), name},
                                                        {QStringLiteral("template"), model::audioSuiteTemplateToJson(*t)}})
                                  .toJson(QJsonDocument::Compact));
                auto* drag = new QDrag(effectIcon_);
                drag->setMimeData(mime);
                drag->exec(Qt::CopyAction);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AudioSuiteTool::setDrives(const QList<QPair<QString, QString>>& drives, const QString& current) {
    const QString was = drive().isEmpty() ? current : drive();
    const QSignalBlocker block(drive_);
    drive_->clear();
    for (const auto& [name, root] : drives) drive_->addItem(name, root);
    const int i = drive_->findData(was);
    drive_->setCurrentIndex(i >= 0 ? i : 0);
}

QString AudioSuiteTool::drive() const {
    return drive_->currentData().toString();
}

QMenu* AudioSuiteTool::fastMenu() {
    QMenu* menu = fast_->menu();
    menu->clear();
    // "The menu text differs, depending on whether you have In to Out points" (p.799).
    const QString s = scope();
    const bool any = !tracks().isEmpty() && !masterMode_;
    menu->addAction(tr("Set Audio Processing - %1").arg(s), this, [this] { setInScope(); })->setEnabled(any && !plugin_.isEmpty());
    menu->addAction(tr("Render Audio Processing - %1").arg(s), this, [this] { renderInScope(); })->setEnabled(any);
    menu->addAction(tr("Remove Audio Processing - %1").arg(s), this, [this] { removeInScope(); })->setEnabled(any);
    // "Apply an existing AudioSuite template" (p.799): ours lists those in the open bins.
    QMenu* list = menu->addMenu(tr("Templates"));
    list->setObjectName(QStringLiteral("templates"));
    const QList<model::BinItem> items = templates_ ? templates_() : QList<model::BinItem>{};
    for (const model::BinItem& item : items) {
        if (const auto t = model::audioSuiteTemplateOf(item)) {
            const model::AudioSuiteTemplate copy = *t;
            list->addAction(item.name, this, [this, copy] { applyTemplate(copy); });
        }
    }
    list->setEnabled(!list->isEmpty());
    return menu;
}

void AudioSuiteTool::refresh() {
    const QStringList t = tracks();
    trackButton_->setText(t.isEmpty() ? tr("No track") : t.first() + (t.size() > 1 ? QStringLiteral(" +") : QString()));
    trackButton_->setEnabled(!masterMode_);
    const auto shown = masterMode_ ? std::nullopt : current();
    if (shown) {
        plugin_ = shown->effect;
        lastValues_ = shown->params;
    }
    levelButton_->setText(QString::number(level_));
    levelButton_->setEnabled(!masterMode_);
    const bool eq = !masterMode_ && plugin_ == QLatin1String(kEqPlugin);
    const auto info = plugin_.isEmpty() || eq ? std::nullopt : media::findAudioSuitePlugin(plugin_);
    pluginButton_->setText(eq ? tr("EQ") : info ? info->name : shown ? shown->name : tr("None"));
    activateButton_->setEnabled(info.has_value() || eq);
    effectIcon_->setEnabled(info.has_value());
    findSource_->setEnabled(pendingTemplate_.has_value());
    loadSource_->setEnabled(active_ >= 0);
    // The Processing Mode Selection menu follows the plug-in.
    {
        const QSignalBlocker block(mode_);
        const QStringList modes = processingModes();
        QStringList now;
        for (int i = 0; i < mode_->count(); ++i) now << mode_->itemText(i);
        if (now != modes) {
            mode_->clear();
            mode_->addItems(modes);
        }
        mode_->setCurrentIndex(std::clamp(processingMode(), 0, static_cast<int>(modes.size()) - 1));
    }
    // "These lights change to green when a mark In or mark Out exists on the current master clip"
    // (p.802).
    for (const auto& [label, on] : {std::pair{markIn_, clipIn().has_value()}, std::pair{markOut_, clipOut().has_value()}}) {
        label->setStyleSheet(on ? QStringLiteral("QLabel { background-color: #3cb44b; color: black; }") : QString());
    }
    // The status display.
    if (masterMode_) {
        if (active_ < 0) {
            status_->setText(tr("Master clip mode: drag a master clip here"));
        } else {
            const SourceClip& c = clips_.at(active_);
            status_->setText(tr("%1: %2 of %3 tracks, %4")
                                 .arg(c.item.name)
                                 .arg(inputs_.size())
                                 .arg(audioTrackCount(c.item))
                                 .arg(info ? info->name : tr("no plug-in")));
        }
        return;
    }
    if (t.isEmpty()) {
        status_->setText(tr("No audio track selected"));
    } else if (!shown) {
        status_->setText(tr("%1: no Audio Processing effect here").arg(t.first()));
    } else if (!info) {
        status_->setText(tr("%1: %2 is not installed").arg(t.first(), shown->name));
    } else {
        const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
        bool rendered = false;
        if (rec && rec->item.kind == model::BinItemKind::Sequence) {
            const model::Sequence seq = model::Sequence::fromItem(rec->item);
            if (const model::Track* on = seq.track(t.first())) {
                const int i = on->segmentAt(timeline_.composer().state(MonitorId::Record).position());
                rendered = i >= 0 && model::isRendered(on->segments.at(i));
            }
        }
        status_->setText(rendered ? tr("%1: %2, rendered").arg(t.first(), shown->name)
                                  : tr("%1: %2, not rendered").arg(t.first(), shown->name));
    }
}

} // namespace lumenis::ui
