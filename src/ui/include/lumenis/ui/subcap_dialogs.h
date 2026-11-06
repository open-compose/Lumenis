// The SubCap effect's dialogs (Phase 7.4a; docs/spec/subcap.md; FX Guide p.383-386): the Edit Master Caption List
// dialog box ("lists the video track, timecode range, and caption text for each caption in the sequence") and the
// choices of Export Caption Data (which caption tracks, and which wins where two overlap).
#pragma once

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"

#include <QDialog>
#include <QList>
#include <QMap>
#include <QPair>
#include <QStringList>

class QButtonGroup;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;

namespace lumenis::ui {

class MasterCaptionListDialog : public QDialog {
    Q_OBJECT
public:
    // One SubCap segment of the sequence.
    struct Entry {
        model::SegmentRef segment;
        std::int64_t end = 0;
        QString text;
    };
    MasterCaptionListDialog(const QList<Entry>& entries, const TimecodeFormat& format, std::int64_t startFrame, QWidget* parent = nullptr);
    // The entries with the texts as edited.
    QList<Entry> entries() const;
    // For tests: the text of entry `row`.
    void setText(int row, const QString& text);

private:
    void showRow(int row);

    QList<Entry> entries_;
    QTableWidget* table_ = nullptr;
    QPlainTextEdit* text_ = nullptr;
    int shown_ = -1;
};

class ExportCaptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportCaptionsDialog(const QStringList& tracks, QWidget* parent = nullptr);
    QStringList tracks() const;
    bool topWins() const;
    void setTracks(const QStringList& tracks);
    void setTopWins(bool top);

private:
    QListWidget* tracks_ = nullptr;
    QButtonGroup* conflict_ = nullptr;
};

// The Global Properties dialog box (p.383, p.504; 7.4a2): the Contacts, Custom and Program information (limited as
// EBU N19 limits it), the Stylesheets (XML files in `folder`, p.387-388) and the Synchronize choices (Clip, Track or
// Sequence for each parameter and information item, p.384-385).
class SubcapPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    SubcapPropertiesDialog(const model::Effect& effect, const QString& folder, QWidget* parent = nullptr);
    // The effect with the dialog's information, synchronize choices and any stylesheet applied.
    model::Effect effect() const;

    // For tests: an information field by its name ("Publisher", "Language", "Creation Date" as yyyy-MM-dd ...), a
    // synchronize choice, and the Stylesheets tab's Create (with its name), Apply, Delete and Set Default.
    void setField(const QString& name, const QString& value);
    void setSync(const QString& name, const QString& mode);
    bool createStylesheet(const QString& name);
    bool applyStylesheet(const QString& name);
    bool deleteStylesheet(const QString& name);
    bool setDefaultStylesheet(const QString& name);
    QStringList stylesheets() const;

    // The countries and EBU N19 language codes the menus offer: {name, code}.
    static QList<QPair<QString, QString>> countries();
    static QList<QPair<QString, QString>> languages();

private:
    void refreshStylesheets();
    void fill();

    model::Effect effect_;
    QString folder_;
    QMap<QString, QLineEdit*> texts_;
    QComboBox* country_ = nullptr;
    QComboBox* language_ = nullptr;
    QDateEdit* creation_ = nullptr;
    QDateEdit* revision_ = nullptr;
    QPlainTextEdit* custom_ = nullptr;
    QListWidget* stylesheets_ = nullptr;
    QMap<QString, QComboBox*> sync_;
};

// Stylesheet files (p.387: "Caption stylesheets are XML files"): an effect's SubCap parameters and information (not its
// caption text) as <CaptionStylesheet>, and back. The default stylesheet's name is kept in `folder`/default.txt.
bool writeSubcapStylesheet(const QString& path, const model::Effect& effect);
bool applySubcapStylesheet(const QString& path, model::Effect& effect);
QString defaultSubcapStylesheet(const QString& folder);

// The SubCap segments of a sequence for the Master Caption List, by track from V1 up and in time.
QList<MasterCaptionListDialog::Entry> subcapEntries(const model::Sequence& sequence);

} // namespace lumenis::ui
