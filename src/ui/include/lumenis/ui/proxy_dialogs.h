#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;

// The Create Proxies window (Editing Guide p.508; 8.7a, docs/spec/proxy-media.md): "Select either a local drive or choose an
// Avid NEXIS workspace ... Processing Options for creating proxies include 'Application' or 'Background Process'". Ours: the
// media drives (media::mediaDrives()) stand for both; MainWindow::createProxies opens it.
namespace lumenis::ui {

class CreateProxiesDialog : public QDialog {
    Q_OBJECT

public:
    // `drives` as "name\troot" pairs are the drive menu's entries; the first is chosen.
    explicit CreateProxiesDialog(const QList<std::pair<QString, QString>>& drives, QWidget* parent = nullptr);

    // The chosen drive's root.
    QString drive() const;
    // "Background Process" rather than "Application".
    bool background() const;
    void setBackground(bool on);

private:
    QComboBox* drives_ = nullptr;
    QComboBox* processing_ = nullptr;
};

} // namespace lumenis::ui
