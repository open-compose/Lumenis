#pragma once

#include <QDialog>
#include <QIcon>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

namespace lumenis::ui {

class WorkspaceManager;

// The workspace toolbar on the right edge of the main window (guide
// p.87-89). Click a workspace to switch to it; right-click to show icons
// and text, icons only, or hide the bar.
class WorkspaceBar : public QWidget {
    Q_OBJECT

public:
    explicit WorkspaceBar(WorkspaceManager& workspaces, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    // Where a workspace's button is, for tests.
    QRect itemRect(int index) const;

    // The toolbar icon for a workspace (our own drawings).
    static QIcon iconFor(const QString& name, bool active);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void sync();

    WorkspaceManager& workspaces_;
};

// Windows > Workspaces > New Workspace (p.86): a name, and optionally the
// workspace to base it on.
class NewWorkspaceDialog : public QDialog {
    Q_OBJECT

public:
    NewWorkspaceDialog(const WorkspaceManager& workspaces, QWidget* parent = nullptr);

    QString name() const;
    // Empty unless "Based on Workspace" is selected.
    QString basedOn() const;

private:
    void validate();

    const WorkspaceManager& workspaces_;
    QLineEdit* name_ = nullptr;
    QCheckBox* basedOnBox_ = nullptr;
    QComboBox* basedOn_ = nullptr;
    QPushButton* ok_ = nullptr;
};

} // namespace lumenis::ui
