#pragma once

#include "lumenis/interop/cut_list.h"
#include "lumenis/interop/edl.h"
#include "lumenis/interop/otio.h"
#include "lumenis/model/bin.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QToolButton;

namespace lumenis::ui {

// Tools > List Tool (Editing Guide p.944-960), for EDLs and OpenTimelineIO files (the OTIO output
// format and the Default OpenTimelineIO setting, p.969). Left: Active Setting and Output Format,
// then the Input tab (the sequence, with Load and Clear; a button for each track; the Title), the
// List Options tab and the Formatting tab. Right: Preview, Save List and Clear List, and the Master
// EDL and Sources tabs that show the list. The EDL Output Types written are CMX_3600 and File_129
// (interop/edl.h), the cut list types Columnar, TabbedLists, WebLists and XML (interop/cut_list.h, 9.4a-b,
// with the List Options' Cut List box); the other output formats (CMX_DigitalCut, CMX_Transfer, Cuedos,
// File_16, File_32) are listed dimmed, as are the options not yet carried out (docs/spec/ui-inventory.md §19).
class ListTool : public QWidget {
    Q_OBJECT

public:
    // How the list finds a segment's clip (for its reel and timecode): the clip's MobID and id.
    using Resolver = std::function<std::optional<model::BinItem>(const MobId& mob, const QString& clipId)>;

    explicit ListTool(QWidget* parent = nullptr);

    // Step 3 (p.948): the sequence to list. Its tracks fill the Tracks pane, all on (step 4); the
    // Title is the sequence's name. Clear empties the tool.
    void setSequence(const model::BinItem& sequence, Resolver resolve);
    void clearSequence();
    bool hasSequence() const { return sequence_.has_value(); }
    const std::optional<model::BinItem>& sequence() const { return sequence_; }

    // The export options the tabs say: for an EDL, and for OTIO (the tracks and clips only).
    interop::edl::ExportOptions options() const;
    interop::otio::ExportOptions otioOptions() const;
    interop::cutlist::Options cutListOptions() const;
    // Whether the Output Format is a cut list type (Columnar, TabbedLists, WebLists, XML), and whether Change List is
    // chosen with it (p.952-954).
    bool isCutList() const;
    bool isChangeList() const;
    // A change list's old version (Load beside Old Sequence; MainWindow answers loadOldRequested with the Record
    // monitor's sequence); the new version is the sequence (setSequence). setChangeSequences gives several reels.
    void setOldSequence(const model::BinItem& sequence);
    void setChangeSequences(const QList<model::BinItem>& olds, const QList<model::BinItem>& news, Resolver resolve);
    interop::cutlist::ChangeOptions changeListOptions() const;
    // The project's film type (ProjectSettings::filmType), for the cut lists' feet and frames; empty for video projects.
    void setFilmType(const QString& filmType) { filmType_ = filmType; }
    void setAspect(double aspect) { aspect_ = aspect; }
    // Whether the Output Format is OTIO (Save List then writes an .otio file).
    bool isOtio() const;
    // Preview (step 8): makes the list and shows it in Master EDL, its reels in Sources. Returns the
    // list; empty without a sequence. The warnings go to message().
    QString preview();
    // Save List (step 9): the list (made first if there is none) written to `path`.
    bool saveList(const QString& path, QString* error = nullptr);

    // Where Save List writes: asks with the system's save dialog unless set (tests set it). Empty:
    // not saved.
    std::function<QString(const QString& suggested)> savePath;
    // Where a saved WebLists page is shown: the system's browser unless set (tests set it).
    std::function<void(const QString& page)> openPage;

    // The widgets, for tests.
    QComboBox* outputFormat() const { return format_; }
    QComboBox* activeSetting() const { return setting_; }
    QLineEdit* title() const { return title_; }
    QPlainTextEdit* masterList() const { return master_; }
    QPlainTextEdit* sourcesList() const { return sources_; }
    QPushButton* loadButton() const { return load_; }
    QPushButton* previewButton() const { return preview_; }
    QPushButton* saveButton() const { return save_; }
    QCheckBox* optimizeBox() const { return optimize_; }
    QSpinBox* startEvent() const { return startEvent_; }
    QWidget* cutListOptionsBox() const { return cutOptions_; }
    QWidget* changeListOptionsBox() const { return changeOptions_; }
    QRadioButton* changeListButton() const { return changeMode_; }
    // The Tracks pane's button for a track, or null.
    QToolButton* trackButton(const QString& track) const;

signals:
    // Load (p.945): MainWindow answers with setSequence() for the Record monitor's sequence.
    void loadRequested();
    void loadOldRequested();
    // A line for the Console: the list's warnings, a save.
    void message(const QString& line);

private:
    void rebuildTracks();
    void saveClicked();
    void formatChanged();
    // The Tracks pane's tracks that are on ("-" alone when none is, so that nothing is listed).
    QStringList listedTracks() const;

    std::optional<model::BinItem> sequence_;
    Resolver resolve_;
    QString made_; // the last list made, for Save List
    QMap<QString, QByteArray> madeFiles_; // a WebLists list's files
    QString filmType_;
    double aspect_ = 16.0 / 9.0;

    QComboBox* setting_ = nullptr;
    QComboBox* format_ = nullptr;
    QLabel* sequenceLabel_ = nullptr;
    QPushButton* load_ = nullptr;
    QPushButton* clear_ = nullptr;
    QWidget* tracks_ = nullptr;
    QHBoxLayout* trackRow_ = nullptr;
    QLineEdit* title_ = nullptr;
    QCheckBox* pictureDissolves_ = nullptr;
    QCheckBox* soundDissolves_ = nullptr;
    QCheckBox* blackEdits_ = nullptr;
    QCheckBox* clipNames_ = nullptr;
    QCheckBox* markers_ = nullptr;
    QCheckBox* optimize_ = nullptr;
    QComboBox* s3dContributor_ = nullptr;
    QSpinBox* startEvent_ = nullptr;
    // The Cut List options (p.960-962, p.964).
    QWidget* cutOptions_ = nullptr;
    QRadioButton* changeMode_ = nullptr;
    QWidget* oldRow_ = nullptr;
    QLabel* oldLabel_ = nullptr;
    QList<model::BinItem> oldSequences_;
    QList<model::BinItem> newSequences_; // several reels; empty: the sequence alone
    QWidget* changeOptions_ = nullptr;
    QCheckBox* showOnlyChanges_ = nullptr;
    QCheckBox* combineDeletions_ = nullptr;
    QCheckBox* previewCode_ = nullptr;
    QCheckBox* changePull_ = nullptr;
    QCheckBox* changeDiscard_ = nullptr;
    QCheckBox* assemble_ = nullptr;
    QCheckBox* sceneAssemble_ = nullptr;
    QCheckBox* optical_ = nullptr;
    QCheckBox* keyFrames_ = nullptr;
    QCheckBox* opticalFootage_ = nullptr;
    QCheckBox* dupe_ = nullptr;
    QSpinBox* assumeHandles_ = nullptr;
    QSpinBox* printHandles_ = nullptr;
    QCheckBox* pull_ = nullptr;
    QComboBox* firstSort_ = nullptr;
    QComboBox* secondSort_ = nullptr;
    QComboBox* pullOrder_ = nullptr;
    QCheckBox* shortCuts_ = nullptr;
    QSpinBox* shortCutFrames_ = nullptr;
    QCheckBox* jumpCuts_ = nullptr;
    QSpinBox* jumpCutFrames_ = nullptr;
    QCheckBox* reelNames_ = nullptr;
    QCheckBox* clipComments_ = nullptr;
    QSpinBox* startAt_ = nullptr;
    QPushButton* preview_ = nullptr;
    QPushButton* save_ = nullptr;
    QPushButton* clearList_ = nullptr;
    QTabWidget* listTabs_ = nullptr;
    QPlainTextEdit* master_ = nullptr;
    QPlainTextEdit* sources_ = nullptr;
};

} // namespace lumenis::ui
