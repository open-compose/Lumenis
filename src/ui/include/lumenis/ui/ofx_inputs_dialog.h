#pragma once

// The OFX Optional Inputs dialog (ROADMAP 6.11c1; docs/spec/ofx-host.md), our name for Avid's "AVX Optional Inputs
// dialog box" (FX Guide p.161): "If the plug-in supports a variable number of input tracks, the AVX Optional Inputs
// dialog box opens. Click the Number of Tracks menu, select the number of video inputs, and then click OK." The menu
// runs from the plug-in's required inputs to all of them (media::OfxPluginInfo::requiredInputs, maxInputs); the
// default is all of them (ours). The Timeline asks it through Timeline::setOptionalInputsChooser.

#include <QDialog>

#include <optional>

class QComboBox;

namespace lumenis::media {
struct OfxPluginInfo;
}

namespace lumenis::ui {

class OfxOptionalInputsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OfxOptionalInputsDialog(const media::OfxPluginInfo& plugin, QWidget* parent = nullptr);

    // The number of tracks chosen.
    int count() const;
    QComboBox* numberOfTracks() const { return tracks_; }

    // Opens the dialog for `plugin`; the number chosen, or nothing when cancelled.
    static std::optional<int> ask(const media::OfxPluginInfo& plugin, QWidget* parent);

private:
    QComboBox* tracks_ = nullptr;
};

} // namespace lumenis::ui
