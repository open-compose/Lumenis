#pragma once

#include "lumenis/media/mixdown.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;

namespace lumenis::ui {

// Timeline > Mixdown > Audio > To Sequence: the Audio Mixdown dialog box (Editing Guide p.731-732;
// docs/spec/audio-mixdown.md). The Source Tracks and Range areas show what is mixed; Mono, Dual Mono,
// Stereo, 5.1 or 7.1 and the target track; Include Master Fader; a bin and a drive; Save Premix
// Sequence. The layout is ours: the guide's picture is not described.
class AudioMixdownDialog : public QDialog {
    Q_OBJECT

public:
    struct Setup {
        QStringList sourceTracks;              // the selected audio tracks
        QString rangeStart;                    // timecodes of the range's first frame
        QString rangeEnd;                      // and of the frame after its last
        QStringList emptyTracks;               // model::emptyAudioTracks(), in order
        QHash<QString, model::AudioFormat> formats; // the empty tracks' formats; Mono when not listed
        QList<QPair<QString, QString>> bins;   // (id, name) of the open bins
        QString activeBin;                     // the one chosen first
        QString drive;                         // the Media Creation setting's drive root, chosen first
    };
    explicit AudioMixdownDialog(const Setup& setup, QWidget* parent = nullptr);

    media::MixdownFormat format() const;
    void setFormat(media::MixdownFormat format);
    // The target tracks: one for Mono, two for Dual Mono, one of the format for Stereo, 5.1 and 7.1.
    // Empty: new tracks (the first choice when there are not enough empty ones, p.732).
    QStringList targetTracks() const;
    bool includeMasterFader() const;
    void setIncludeMasterFader(bool on);
    QString bin() const;
    QString drive() const;
    bool savePremixSequence() const;
    void setSavePremixSequence(bool on);

    QComboBox* targetBox() const { return target_; }

private:
    // The target menu for the format: the empty tracks of its track format (pairs of mono ones for
    // Dual Mono) and New Track(s).
    void fillTargets();

    Setup setup_;
    QButtonGroup* formats_ = nullptr;
    QComboBox* target_ = nullptr;
    QCheckBox* master_ = nullptr;
    QComboBox* bins_ = nullptr;
    QComboBox* drives_ = nullptr;
    QCheckBox* premix_ = nullptr;
};

} // namespace lumenis::ui
