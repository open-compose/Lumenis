#pragma once

// The windows of transcripts out (Editing Guide p.450-456; docs/spec/transcripts.md, 8.2d):
// * TranscribeTracksDialog, the Transcribe window: "select the audio tracks from your sequence that you want to include"
//   (Export Transcript and Export SubCap of a sequence, Create SubCap), with "Use Marks" and "Limit characters per
//   caption" for captions (p.452-453);
// * ManageTranscriptsDialog, Manage Transcripts (2026.8 guide p.455-456): the project's transcribed clips with Delete,
//   Import of a shared transcripts file, Export and Done. It changes nothing itself: MainWindow answers its signals and
//   gives it the list again.

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace lumenis::ui {

class TranscribeTracksDialog : public QDialog {
    Q_OBJECT

public:
    // `tracks`: the sequence's audio tracks, all checked at first. `captions`: show Use Marks and Limit characters per
    // caption.
    TranscribeTracksDialog(const QStringList& tracks, bool captions, QWidget* parent = nullptr);
    QStringList tracks() const;
    void setTracks(const QStringList& checked);
    bool useMarks() const;
    void setUseMarks(bool on);
    // 0 when not limited.
    int characterLimit() const;
    void setCharacterLimit(int characters); // 0 turns the limit off

private:
    QListWidget* list_ = nullptr;
    QCheckBox* marks_ = nullptr;
    QCheckBox* limit_ = nullptr;
    QSpinBox* characters_ = nullptr;
};

class ManageTranscriptsDialog : public QDialog {
    Q_OBJECT

public:
    struct Entry {
        QString mob;
        QString clip;     // the clip's name in the project
        QString language; // the transcript's language
        QString created;  // when it was made, as text
        bool edited = false;
    };
    explicit ManageTranscriptsDialog(QWidget* parent = nullptr);
    void setEntries(const QList<Entry>& entries);
    QList<Entry> entries() const { return entries_; }
    // The selected rows' MobIDs; select() selects rows by MobID (tests).
    QStringList selected() const;
    void select(const QStringList& mobs);
    QTreeWidget* list() const { return list_; }

signals:
    void deleteRequested(const QStringList& mobs);
    void importRequested();
    void exportRequested();

private:
    QTreeWidget* list_ = nullptr;
    QPushButton* delete_ = nullptr;
    QList<Entry> entries_;
};

} // namespace lumenis::ui
