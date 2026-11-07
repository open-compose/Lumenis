#pragma once

// The Manage Speaker Names window (Editing Guide p.445; docs/spec/transcripts.md, 8.2b2b), opened by the Manage Speakers
// button of File > Settings > Project > Transcript: "a list of all available speakers that have been added or edited in
// transcripts for your current project", double-click to rename, "+" to add, Merge for several selected names, Clear or
// Delete and Done.
//
// The window changes nothing itself: each action goes out as changeRequested(), which MainWindow applies to the project's
// list (model::ProjectSpeakers) and its transcripts, then gives the window the names again with setNames().

#include <QDialog>
#include <QStringList>

class QListWidget;
class QPushButton;

namespace lumenis::ui {

struct SpeakerNameChange {
    enum class Kind { Add, Rename, Merge, Delete, Clear };
    Kind kind = Kind::Add;
    QStringList names; // the names acted on (one but for Merge)
    QString to;        // Add's, Rename's and Merge's resulting name
};

class SpeakerNamesDialog : public QDialog {
    Q_OBJECT

public:
    explicit SpeakerNamesDialog(QWidget* parent = nullptr);

    // `names`: the project's list and the names in its transcripts; `used`: those in its transcripts, shown not dimmed
    // ("If a speaker name in the list appears partially dimmed, this indicates that it does not appear in any transcripts
    // for this project"). Keeps the selection by name.
    void setNames(const QStringList& names, const QStringList& used);
    QStringList names() const;
    bool isUsed(const QString& name) const { return used_.contains(name); }

    // The actions, as the buttons and a double-click do them (for tests too). select() selects names in the list.
    void select(const QStringList& names);
    QStringList selected() const;
    void add(const QString& name);
    void rename(const QString& from, const QString& to);
    // Merge the selected names into the first of them; false with fewer than two.
    bool mergeSelected();
    // Delete (unused names) or Clear (used names) the selected ones; false with none.
    bool clearSelected();
    // "the Clear/Delete button will say Delete" for a dimmed name, "Clear" otherwise.
    QString clearButtonText() const;

signals:
    void changeRequested(const lumenis::ui::SpeakerNameChange& change);

private:
    void refreshButtons();

    QListWidget* list_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* merge_ = nullptr;
    QPushButton* clear_ = nullptr;
    QStringList used_;
    bool updating_ = false;
};

} // namespace lumenis::ui
