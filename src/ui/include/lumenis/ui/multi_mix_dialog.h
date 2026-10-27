#pragma once

#include "lumenis/media/mixdown.h"

#include <QDialog>
#include <QList>
#include <QMap>
#include <QPair>
#include <QStringList>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QToolButton;

// The Multi-Mix Tool (Timeline > Mixdown > Audio > Multiple Mixes; Editing Guide p.732-734;
// docs/spec/multi-mix.md), laid out as the pictures on p.733-734:
// - at the top, Select Setting, Bin, Drive, Use Marks, Create New Sequence (named), Exclude Video and
//   Data Tracks, and Processing Options (Application; Distributed Processing dimmed);
// - the matrix: a row per audio track of the sequence, a column per output with its number button
//   (the menu: Edit Mix, Set to Mono / Stereo / Dual Mono / Surround 5.1 / Surround 7.1, Add Output,
//   Remove Output, Clear Mix) and the Master Fader button above it, and the + button to add one;
// - Save Setting, Save Setting As..., Mix and Close.
// The dialog edits a media::MultiMix; MainWindow makes the mixes when Mix closes it with Accepted,
// and keeps the saved settings (Project settings, kind "Audio Multi-Mix").
namespace lumenis::ui {

class MultiMixDialog : public QDialog {
    Q_OBJECT

public:
    struct Setup {
        QStringList tracks;                     // the sequence's audio tracks, the rows
        QList<QPair<QString, QString>> bins;    // (id, name) of the bins that can be changed
        QString activeBin;
        QList<QPair<QString, QString>> drives;  // (name, root)
        QString drive;                          // root chosen first
        QMap<QString, media::MultiMix> settings; // saved settings by name
        QString setting;                        // the one chosen first; empty: Untitled
        bool canMix = true; // false when opened from the Settings window (p.734)
    };
    MultiMixDialog(const Setup& setup, QWidget* parent = nullptr);

    const media::MultiMix& mix() const { return mix_; }
    void setMix(const media::MultiMix& mix);
    QString bin() const;
    QString drive() const;
    QString settingName() const;

    // The matrix's edits, as its buttons and menus make them.
    void addOutput();
    void removeOutput(int output);
    void clearMix(int output);
    void setFormat(int output, media::MixdownFormat format);
    void toggleInput(int output, const QString& track);
    // Alt-click on an output: every input of the column on, or all off when all are on (p.733).
    void toggleColumn(int output);
    void setMasterFader(int output, bool on);
    // Edit Mix (p.734): its name, format and Master Fader. False when cancelled.
    bool editMix(int output);
    // Save Setting, under the chosen name (Untitled asks for one), and Save Setting As.
    bool saveSetting();
    bool saveSettingAs();

    // Hooks for tests and MainWindow: the Edit Mix dialog's answer, the name Save Setting As asks
    // for, and where a saved setting goes.
    std::function<std::optional<media::MixOutput>(const media::MixOutput&)> editMixAsk;
    std::function<std::optional<QString>()> nameAsk;
    std::function<void(const QString& name, const media::MultiMix& mix)> save;

    QToolButton* outputButton(int output) const { return outputButtons_.value(output); }
    QToolButton* masterButton(int output) const { return masterButtons_.value(output); }
    QToolButton* inputButton(int output, const QString& track) const;
    QPushButton* mixButton() const { return mixButton_; }
    QComboBox* settingMenu() const { return setting_; }
    // The output's menu, filled as its button shows it.
    void fillOutputMenu(QMenu* menu, int output);

protected:
    // Shift held while the pointer passes over an input toggles it (p.733).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Remakes the matrix from mix_, and the Mix button's state from multiMixProblem().
    void rebuild();
    void takeOptions(); // the check boxes and name into mix_
    // Runs `f` once the current event is over (QueuedConnection).
    void later(const std::function<void()>& f);

    Setup setup_;
    media::MultiMix mix_;
    QComboBox* setting_ = nullptr;
    QComboBox* bins_ = nullptr;
    QComboBox* drives_ = nullptr;
    QCheckBox* useMarks_ = nullptr;
    QCheckBox* newSequence_ = nullptr;
    QLineEdit* sequenceName_ = nullptr;
    QCheckBox* exclude_ = nullptr;
    QWidget* matrix_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QList<QToolButton*> outputButtons_;
    QList<QToolButton*> masterButtons_;
    QMap<QPair<int, QString>, QToolButton*> inputs_;
    QPushButton* mixButton_ = nullptr;
    QLabel* problem_ = nullptr;
};

} // namespace lumenis::ui
