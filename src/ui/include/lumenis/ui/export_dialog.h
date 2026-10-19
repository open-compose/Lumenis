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
    // `settings`: the Export settings' names, the active one at `active`.
    ExportAsDialog(const QString& path, const QStringList& settings, int active, QWidget* parent = nullptr);

    QString path() const;
    void setPath(const QString& path);
    // The Export setting chosen, by its place in the list.
    int setting() const;
    void setSetting(int index);

    QLineEdit* pathEdit() const { return path_; }
    QPushButton* optionsButton() const { return options_; }

signals:
    // Options (p.869): the chosen setting's dialog.
    void optionsRequested(int setting);

private:
    void browse();

    QLineEdit* path_ = nullptr;
    QComboBox* settings_ = nullptr;
    QPushButton* options_ = nullptr;
    QPushButton* save_ = nullptr;
};

} // namespace lumenis::ui
