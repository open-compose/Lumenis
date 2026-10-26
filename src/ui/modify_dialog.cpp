#include "lumenis/ui/modify_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

// The Modify dialog (modify_dialog.h; Editing Guide p.279-281, p.671-672; docs/spec/multichannel.md).
// The formats are the model's (lumenis/model/multichannel.h); MainWindow stores the clips it gives
// back.

namespace lumenis::ui {

namespace {

QString buttonText(std::optional<model::AudioFormat> f) {
    if (!f) return QObject::tr("Mixed");
    switch (*f) {
    case model::AudioFormat::Mono: return QObject::tr("Mono");
    case model::AudioFormat::Stereo: return QObject::tr("Stereo");
    case model::AudioFormat::Surround51: return QObject::tr("5.1");
    case model::AudioFormat::Surround71: return QObject::tr("7.1");
    }
    return {};
}

// The format a clip's channels `first` and `first + 1` have: Mono, the group that starts there, or
// nothing when the pair lies inside or across a group that starts elsewhere.
std::optional<model::AudioFormat> pairOf(const model::BinItem& clip, int first) {
    for (const model::ChannelGroup& g : model::trackFormatsOf(clip)) {
        const int last = g.first + model::channelCount(g.format) - 1;
        if (g.first == first) return g.format;
        if (first + 1 >= g.first && first <= last) return std::nullopt;
    }
    return model::AudioFormat::Mono;
}

} // namespace

ModifyDialog::ModifyDialog(const QList<model::BinItem>& clips, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("modifyDialog"));
    setWindowTitle(tr("Modify"));
    // The Modify Options menu, as the table on p.280 lists them.
    options_ = new QComboBox(this);
    options_->setObjectName(QStringLiteral("modifyOptions"));
    const QStringList names{tr("Set Timecode Drop/Non-drop"), tr("Set Timecode By Field"), tr("Increment Timecode"),
                            tr("Decrement Timecode"),          tr("Set Key Number Generic (Prefix)"),
                            tr("Set Pullin"),                  tr("Set Tracks"),
                            tr("Set Source"),                  tr("Set Disk Label"),
                            tr("Set Format"),                  tr("Set Multichannel Audio"),
                            tr("Active Format Description")};
    options_->addItems(names);
    auto* items = qobject_cast<QStandardItemModel*>(options_->model());
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i) != tr("Set Multichannel Audio") && items) items->item(i)->setEnabled(false);
    }
    options_->setCurrentText(tr("Set Multichannel Audio"));

    for (const model::BinItem& c : clips) channels_ = std::max(channels_, model::audioChannelsOf(c));
    const int pairs = channels_ / 2;
    for (int p = 0; p < pairs; ++p) {
        std::optional<model::AudioFormat> f;
        bool first = true;
        bool mixed = false;
        for (const model::BinItem& c : clips) {
            if (model::audioChannelsOf(c) < 2 * p + 2) continue;
            const auto here = pairOf(c, 2 * p + 1);
            if (first) {
                f = here;
                first = false;
            } else if (here != f) {
                mixed = true;
            }
        }
        formats_ << (mixed ? std::nullopt : (first ? std::optional(model::AudioFormat::Mono) : f));
    }

    // The tracks across, a Format button beneath each pair (p.671).
    auto* grid = new QGridLayout;
    for (int c = 0; c < channels_; ++c) {
        auto* label = new QLabel(QStringLiteral("A%1").arg(c + 1), this);
        label->setAlignment(Qt::AlignCenter);
        grid->addWidget(label, 0, c);
    }
    for (int p = 0; p < pairs; ++p) {
        auto* b = new QToolButton(this);
        b->setObjectName(QStringLiteral("format:%1").arg(p));
        b->setToolTip(tr("Format"));
        b->setPopupMode(QToolButton::MenuButtonPopup);
        auto* menu = new QMenu(b);
        for (const auto& [text, format] :
             {std::pair{tr("Mono tracks"), model::AudioFormat::Mono}, std::pair{tr("Stereo tracks"), model::AudioFormat::Stereo},
              std::pair{tr("5.1 Surround sound tracks"), model::AudioFormat::Surround51},
              std::pair{tr("7.1 Surround sound tracks"), model::AudioFormat::Surround71}}) {
            QAction* a = menu->addAction(text, this, [this, p, f = format] { setPairFormat(p, f); });
            a->setEnabled(fits(p, format));
        }
        b->setMenu(menu);
        connect(b, &QToolButton::clicked, this, [this, p] { cycle(p); });
        grid->addWidget(b, 1, 2 * p, 1, 2);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(options_);
    if (channels_ == 0) layout->addWidget(new QLabel(tr("The selected clips have no audio tracks."), this));
    layout->addLayout(grid);
    layout->addWidget(buttons);
    showFormats();
}

bool ModifyDialog::fits(int pair, model::AudioFormat format) const {
    return 2 * pair + model::channelCount(format) <= std::max(channels_, 2);
}

bool ModifyDialog::covered(int pair) const {
    for (int p = 0; p < pair; ++p) {
        const auto f = formats_.at(p);
        if (f && p + (model::channelCount(*f) + 1) / 2 > pair) return true;
    }
    return false;
}

std::optional<model::AudioFormat> ModifyDialog::pairFormat(int pair) const {
    return pair >= 0 && pair < formats_.size() ? formats_.at(pair) : std::nullopt;
}

void ModifyDialog::setPairFormat(int pair, std::optional<model::AudioFormat> format) {
    if (pair < 0 || pair >= formats_.size() || covered(pair)) return;
    if (format && !fits(pair, *format)) return;
    formats_[pair] = format;
    // The pairs a surround group takes are its own.
    if (format) {
        for (int p = pair + 1; p < pair + (model::channelCount(*format) + 1) / 2 && p < formats_.size(); ++p) {
            formats_[p] = model::AudioFormat::Mono;
        }
    }
    showFormats();
}

void ModifyDialog::cycle(int pair) {
    const model::AudioFormat order[] = {model::AudioFormat::Mono, model::AudioFormat::Stereo, model::AudioFormat::Surround51,
                                        model::AudioFormat::Surround71};
    const auto now = pairFormat(pair);
    int i = 0;
    if (now) i = static_cast<int>(std::find(std::begin(order), std::end(order), *now) - std::begin(order)) + 1;
    for (int k = 0; k < 4; ++k) {
        const model::AudioFormat next = order[(i + k) % 4];
        if (fits(pair, next)) {
            setPairFormat(pair, next);
            return;
        }
    }
}

void ModifyDialog::showFormats() {
    for (int p = 0; p < formats_.size(); ++p) {
        QToolButton* b = formatButton(p);
        if (!b) continue;
        const bool under = covered(p);
        b->setEnabled(!under);
        b->setText(under ? QStringLiteral("—") : buttonText(formats_.at(p)));
    }
}

QToolButton* ModifyDialog::formatButton(int pair) const {
    return findChild<QToolButton*>(QStringLiteral("format:%1").arg(pair));
}

QList<model::BinItem> ModifyDialog::apply(const QList<model::BinItem>& clips, QStringList* errors) const {
    QList<model::BinItem> out;
    for (const model::BinItem& clip : clips) {
        if (clip.kind != model::BinItemKind::MasterClip && clip.kind != model::BinItemKind::AudioClip) {
            if (errors) *errors << tr("%1: track formats for sequences, group clips, or subclips cannot be modified.").arg(clip.name);
            continue;
        }
        QList<model::ChannelGroup> groups = model::trackFormatsOf(clip);
        const int channels = model::audioChannelsOf(clip);
        for (int p = 0; p < formats_.size(); ++p) {
            const auto f = formats_.at(p);
            if (!f || covered(p) || 2 * p + 2 > channels) continue;
            // The pair's channels, or the surround group's, lose what they had.
            const int first = 2 * p + 1;
            const int last = first + model::channelCount(*f) - 1;
            const int span = std::max(last, first + 1);
            groups.erase(std::remove_if(groups.begin(), groups.end(),
                                        [&](const model::ChannelGroup& g) {
                                            return g.first <= span && g.first + model::channelCount(g.format) - 1 >= first;
                                        }),
                         groups.end());
            if (*f != model::AudioFormat::Mono) groups.append({*f, first});
        }
        model::BinItem changed = clip;
        QString error;
        if (!model::setTrackFormats(changed, groups, &error)) {
            if (errors) *errors << QStringLiteral("%1: %2").arg(clip.name, error);
            continue;
        }
        out << changed;
    }
    return out;
}

} // namespace lumenis::ui
