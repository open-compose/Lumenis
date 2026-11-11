#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QPushButton;

namespace lumenis::ui {

// File > Output > Export to File: the Export As dialog (Editing Guide
// p.868-869). The file to write, and the Export setting that says how:
// one of the User settings of the Export kind (p.861). Options opens the
// chosen setting (p.869). The layout is ours: the guide shows the
// system's save dialog with these controls added.
class ExportAsDialog : public QDialog {
    Q_OBJECT

public:
    // `path`: the file name offered first. `settings`: the Export settings' names, the active one at
    // `active` (an index into `settings`). Save stays disabled while the file name is blank.
    ExportAsDialog(const QString& path, const QStringList& settings, int active, QWidget* parent = nullptr);

    // The file name typed or browsed to, trimmed of spaces.
    QString path() const;
    void setPath(const QString& path);
    // The Export setting chosen, by its place in the list.
    int setting() const;
    void setSetting(int index);
    // The Export Setting menu again, with `active` chosen (after Options' Save As made a new setting, p.870).
    void setSettings(const QStringList& settings, int active);
    // The file type Browse offers and keeps (p.869: the file name extension stays the format's):
    // its extension without the dot ("aaf", the default; "avb") and its name in the save dialog.
    void setFileType(const QString& suffix, const QString& filter);

    // The widgets, for tests.
    QLineEdit* pathEdit() const { return path_; }
    QPushButton* optionsButton() const { return options_; }

signals:
    // Options (p.869-870): the chosen setting's Export Settings dialog box. `setting` is setting(); MainWindow opens
    // that Export setting's dialog with Save and Save As (SettingsDialog::useSaveButtons).
    void optionsRequested(int setting);

private:
    // Browse: the system's save dialog, filtered to the file type (setFileType).
    void browse();

    QLineEdit* path_ = nullptr;
    QComboBox* settings_ = nullptr;
    QPushButton* options_ = nullptr;
    QPushButton* save_ = nullptr;
    QString suffix_ = QStringLiteral("aaf");
    QString filter_ = QStringLiteral("AAF files (*.aaf)");
};

} // namespace lumenis::ui
