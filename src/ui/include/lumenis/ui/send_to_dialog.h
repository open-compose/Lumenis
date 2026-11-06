// The Send To dialog box and the Pro Tools template options (Phase 7.1c2; docs/spec/send-to.md; Editing Guide
// p.863-867). The dialog only collects choices: MainWindow::sendTo runs the export with them.
#pragma once

#include "lumenis/model/send_to.h"

#include <QDialog>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace lumenis::ui {

// "The Send To dialog box opens. The Filename text box displays the name of the sequence or clip you chose" (p.864):
// the file name, the destination folder with Set, Auto Launch with its application, Auto Load Exported File(s) and
// Reveal file, the Export Setting Summary, Options, Save As Template, OK and Cancel.
class SendToDialog : public QDialog {
    Q_OBJECT
public:
    SendToDialog(const model::SendToTemplate& t, const QString& fileName, QWidget* parent = nullptr);

    QString fileName() const;
    QString folder() const { return folder_; }
    void setFolder(const QString& folder);
    // The template as the dialog now has it (folder, Auto Launch and the rest; the Export values set by Options).
    model::SendToTemplate sendToTemplate() const;
    void setExportValues(const QJsonObject& values);
    // The Export Setting Summary's lines ("Export As: AAF", ...), shown read-only.
    void setSummary(const QStringList& lines);

signals:
    // Options: the Export setting's dialog for the template's values (p.865, step 9).
    void optionsRequested();
    // Save As Template (p.865, step 10).
    void saveAsTemplateRequested();

private:
    void chooseFolder();
    void chooseApplication();

    model::SendToTemplate template_;
    QString folder_;
    QLineEdit* name_ = nullptr;
    QLabel* folderLabel_ = nullptr;
    QCheckBox* autoLaunch_ = nullptr;
    QLineEdit* application_ = nullptr;
    QCheckBox* autoLoad_ = nullptr;
    QCheckBox* reveal_ = nullptr;
    QPlainTextEdit* summary_ = nullptr;
};

// "If you are sending to Avid Pro Tools, a template options dialog box opens" (p.863): one of the options of
// model::proToolsOptions().
class ProToolsOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProToolsOptionsDialog(const QString& chosen, QWidget* parent = nullptr);
    QString option() const;
    void setOption(const QString& id);

private:
    QButtonGroup* group_ = nullptr;
};

} // namespace lumenis::ui
