#pragma once

#include "lumenis/core/rational.h"

#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QLineEdit;
class QPushButton;

namespace lumenis::ui {

// File > Input > Import EDL (Editing Guide p.951): the EDL to read, the Frame Rate to read it at
// (an EDL does not say), and the bin the sequence goes into ("Select one of the bins listed inside
// the window or click New Bin", step 6). MainWindow shows the system's open dialog first (step 2);
// the guide's options dialog and Select window then are one dialog here, which is our choice. Project
// Type and Audio
// Mapping (step 4) are shown dimmed until they are understood (docs/spec/ui-inventory.md §19).
class ImportEdlDialog : public QDialog {
    Q_OBJECT

public:
    // A bin the dialog offers: its sidebar id and name.
    struct Bin {
        QString id;
        QString name;
    };
    // `rate`: the Frame Rate offered first (the project's). `bins`: the project's bins; `current`
    // is chosen first (the active bin), else New Bin. OK stays disabled while the file name is blank.
    ImportEdlDialog(const QString& path, const Rational& rate, const QList<Bin>& bins, const QString& current,
                    QWidget* parent = nullptr);

    QString path() const;
    void setPath(const QString& path);
    Rational rate() const;
    void setRate(const Rational& rate);
    // The bin chosen; empty for New Bin.
    QString bin() const;
    void setBin(const QString& id);

    // The rates offered: film, PAL, NTSC and their doubles.
    static const QList<Rational>& rates();

    // The widgets, for tests.
    QLineEdit* pathEdit() const { return path_; }
    QComboBox* rateMenu() const { return rate_; }
    QComboBox* binMenu() const { return bins_; }

private:
    void browse();

    QLineEdit* path_ = nullptr;
    QComboBox* rate_ = nullptr;
    QComboBox* bins_ = nullptr;
    QPushButton* ok_ = nullptr;
};

// File > Output > Export Bin (p.130): "The Export Bin As dialog box opens. Click the Export Bin As
// menu, select the appropriate option, and click OK. The system creates a shot log of only the
// master clips in the bin." The options are the shot log formats of Export As (p.1174): Avid Log
// Exchange works; Tab Delimited is dimmed for later. The file name with Browse is ours: the guide
// does not show where the file is named.
class ExportBinDialog : public QDialog {
    Q_OBJECT

public:
    ExportBinDialog(const QString& path, QWidget* parent = nullptr);

    QString path() const;
    void setPath(const QString& path);
    // "ale" (Avid Log Exchange).
    QString format() const;

    QLineEdit* pathEdit() const { return path_; }
    QComboBox* formatMenu() const { return format_; }

private:
    void browse();

    QLineEdit* path_ = nullptr;
    QComboBox* format_ = nullptr;
    QPushButton* ok_ = nullptr;
};

} // namespace lumenis::ui
