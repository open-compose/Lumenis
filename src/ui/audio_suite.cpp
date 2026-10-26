#include "lumenis/ui/audio_suite.h"

#include "lumenis/model/audio_suite.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QComboBox>
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

// Tools > AudioSuite (audio_suite.h; Editing Guide p.796-800; docs/spec/audiosuite.md). The
// processes are src/media/audio_suite.cpp; the effect is set on segments through
// Timeline::setAudioSuite and rendered through Timeline::renderAudioSuite with MainWindow's renderer
// (media::renderAudioSuite); playback plays rendered effects (media::audioPieces).

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

// A disabled button for a feature that comes later; its tooltip says so.
QToolButton* laterButton(const QString& text, const QString& name, const QString& tip, QWidget* parent) {
    QToolButton* b = smallButton(text, name, tip + QStringLiteral(" (") + QObject::tr("not available yet") + QLatin1Char(')'), parent);
    b->setEnabled(false);
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

AudioSuiteTool::AudioSuiteTool(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("audioSuite"));

    // The default layout (picture and table on p.797), left to right: the effect icon, Audio Loop
    // Play, Render Effect, the Fast menu, the Track Selection Menu, Nested Level Selection, Activate
    // Current Plug-In; below, the status display, the Plug-In Selection menu and the Target Drive
    // menu.
    auto* bar = new QHBoxLayout;
    bar->setSpacing(3);
    bar->addWidget(laterButton(tr("AS"), QStringLiteral("effectIcon"), tr("Effect icon: drag to a bin for a template"), this));
    bar->addWidget(laterButton(tr("Loop"), QStringLiteral("audioLoopPlay"), tr("Audio Loop Play"), this));
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
    bar->addWidget(laterButton(tr("0"), QStringLiteral("nestedLevel"), tr("Nested Level Selection Menu"), this));
    activateButton_ = smallButton(tr("Activate"), QStringLiteral("activatePlugin"), tr("Activate Current Plug-In"), this);
    connect(activateButton_, &QToolButton::clicked, this, [this] { activate(); });
    bar->addWidget(activateButton_);
    bar->addStretch(1);

    auto* bottom = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("status"));
    status_->setFrameShape(QFrame::StyledPanel);
    status_->setMinimumWidth(160);
    bottom->addWidget(status_, 1);
    pluginButton_ = smallButton(tr("None"), QStringLiteral("pluginSelection"), tr("Plug-In Selection"), this);
    pluginButton_->setPopupMode(QToolButton::InstantPopup);
    pluginButton_->setMenu(new QMenu(pluginButton_));
    // The plug-ins by category, then None to take the effect off (ours).
    connect(pluginButton_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* menu = pluginButton_->menu();
        menu->clear();
        QMap<QString, QMenu*> groups;
        for (const media::AudioSuiteInfo& i : media::audioSuitePlugins()) {
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

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("message"));
    message_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addLayout(bottom);
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
    return t.isEmpty() ? std::nullopt : timeline_.audioSuiteAt(t);
}

bool AudioSuiteTool::fail(const QString& text) {
    message_->setText(text);
    return false;
}

bool AudioSuiteTool::choosePlugin(const QString& id) {
    message_->clear();
    if (tracks().isEmpty()) return fail(tr("Select an audio track."));
    if (id.isEmpty()) {
        if (!timeline_.setAudioSuite(tracks(), std::nullopt, false, tr("Remove AudioSuite"))) return fail(timeline_.lastMessage());
        return true;
    }
    const auto info = media::findAudioSuitePlugin(id);
    if (!info) return fail(tr("The AudioSuite plug-in %1 is not installed.").arg(id));
    model::AudioSuiteEffect e;
    e.effect = id;
    e.name = info->name;
    e.params = info->defaults();
    plugin_ = id;
    lastValues_ = e.params;
    if (!timeline_.setAudioSuite(tracks(), e, false, tr("AudioSuite %1").arg(info->name))) return fail(timeline_.lastMessage());
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
    if (!timeline_.setAudioSuite(tracks(), e, false, tr("AudioSuite %1").arg(info->name))) return fail(timeline_.lastMessage());
    return true;
}

AudioSuitePluginDialog* AudioSuiteTool::activate() {
    message_->clear();
    const auto info = media::findAudioSuitePlugin(plugin_);
    if (!info) {
        fail(tr("Choose a plug-in first."));
        return nullptr;
    }
    const auto shown = current();
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
    if (!renderer_) return fail(tr("AudioSuite effects cannot be rendered here."));
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
    if (timeline_.audioSuiteToRender(list, from, to) == 0) return fail(tr("There is no AudioSuite effect to render."));
    const auto done = timeline_.renderAudioSuite(list, from, to, render);
    if (!done) return fail(timeline_.lastMessage());
    message_->setText(tr("%n effect(s) rendered.", "", *done));
    return true;
}

bool AudioSuiteTool::render() {
    return renderRange(false);
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
    if (!timeline_.setAudioSuite(tracks(), e, true, tr("Set AudioSuite - %1").arg(scope()))) return fail(timeline_.lastMessage());
    return true;
}

bool AudioSuiteTool::renderInScope() {
    return renderRange(true);
}

bool AudioSuiteTool::removeInScope() {
    message_->clear();
    if (!timeline_.setAudioSuite(tracks(), std::nullopt, true, tr("Remove AudioSuite - %1").arg(scope()))) {
        return fail(timeline_.lastMessage());
    }
    return true;
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
    const bool any = !tracks().isEmpty();
    menu->addAction(tr("Set AudioSuite - %1").arg(s), this, [this] { setInScope(); })->setEnabled(any && !plugin_.isEmpty());
    menu->addAction(tr("Render AudioSuite - %1").arg(s), this, [this] { renderInScope(); })->setEnabled(any);
    menu->addAction(tr("Remove AudioSuite - %1").arg(s), this, [this] { removeInScope(); })->setEnabled(any);
    return menu;
}

void AudioSuiteTool::refresh() {
    const QStringList t = tracks();
    trackButton_->setText(t.isEmpty() ? tr("No track") : t.first() + (t.size() > 1 ? QStringLiteral(" +") : QString()));
    const auto shown = current();
    if (shown) {
        plugin_ = shown->effect;
        lastValues_ = shown->params;
    }
    const auto info = plugin_.isEmpty() ? std::nullopt : media::findAudioSuitePlugin(plugin_);
    pluginButton_->setText(info ? info->name : shown ? shown->name : tr("None"));
    activateButton_->setEnabled(info.has_value());
    // The status display: what the segment under the position has.
    if (t.isEmpty()) {
        status_->setText(tr("No audio track selected"));
    } else if (!shown) {
        status_->setText(tr("%1: no AudioSuite effect here").arg(t.first()));
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
