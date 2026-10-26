#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/multichannel.h"

#include <QDialog>
#include <QList>

#include <optional>

class QComboBox;
class QToolButton;

namespace lumenis::ui {

// Clip > Modify > Modify Clip (Editing Guide p.279-281): the Modify dialog for the clips selected in a
// bin. Its Modify Options menu lists the guide's options (p.280); Set Multichannel Audio
// (p.671-672) is the one made so far, the others are dimmed.
//
// Set Multichannel Audio shows the selected clips' audio tracks, A1 to An, with a Format button
// beneath each pair (A1A2, A3A4, ...). Clicking a button takes the next format that fits: Mono
// tracks, Stereo tracks, 5.1 and 7.1 surround (these take three or four pairs, whose own buttons are
// then dimmed); its menu picks one. With several clips whose formats differ there, the button starts
// as Mixed tracks, which changes nothing (p.672).
class ModifyDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModifyDialog(const QList<model::BinItem>& clips, QWidget* parent = nullptr);

    // A pair's format: nothing for Mixed tracks; Mono for two mono tracks. Pairs count from 0 (A1A2).
    std::optional<model::AudioFormat> pairFormat(int pair) const;
    void setPairFormat(int pair, std::optional<model::AudioFormat> format);
    int pairCount() const { return static_cast<int>(formats_.size()); }
    // Whether a pair is taken by a surround group that starts on an earlier pair.
    bool covered(int pair) const;
    QToolButton* formatButton(int pair) const;
    QComboBox* optionsMenu() const { return options_; }

    // The clips with the dialog's formats: each pair that is not Mixed replaces the clip's groups over
    // its channels. Clips the formats do not fit, and sequences, subclips and group clips ("Track
    // formats for sequences, group clips, or subclips cannot be modified", p.672), are left out and
    // named in `errors`.
    QList<model::BinItem> apply(const QList<model::BinItem>& clips, QStringList* errors) const;

private:
    void cycle(int pair);
    void showFormats();
    // Whether `format` starting on `pair` fits the channels.
    bool fits(int pair, model::AudioFormat format) const;

    int channels_ = 0;
    QList<std::optional<model::AudioFormat>> formats_;
    QComboBox* options_ = nullptr;
};

} // namespace lumenis::ui
