#pragma once

#include <QList>
#include <QMimeData>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

// Exporting by drag and drop (Editing Guide p.868-869; 11.4b): "Click the clip or sequence from which you want to
// export and drag it to the location (folder or drive) where you want to store the file", with the Export setting set
// up in advance. Bins' drags carry an ExportDragData: besides the bin items it offers the exported files as a URL list
// that is only made when something outside the application asks for it at the drop (a file manager), so drags within
// the application (to another bin, a monitor, the Timeline) export nothing. MainWindow sets the handler that exports.
namespace lumenis::ui {

// Exports the items of bin `binId` with the Export setting in use and returns the files made, or nothing when the
// setting's format cannot be dragged (ALE, tab-delimited and sequential files, p.868) or the export failed.
using ExportDragHandler = std::function<std::optional<QList<QUrl>>(const QString& binId, const QStringList& itemIds)>;
void setExportDragHandler(ExportDragHandler handler);

class ExportDragData : public QMimeData {
    Q_OBJECT

public:
    ExportDragData(const QString& binId, const QStringList& itemIds);

    // text/uri-list among the formats while a handler is set.
    QStringList formats() const override;
    bool hasFormat(const QString& mimeType) const override;

    // The files exported, made the first time (cached). retrieveData() asks for them only while the pointer is outside
    // the application's windows; tests call it to drop as a file manager would.
    QList<QUrl> exportedFiles() const;

protected:
    QVariant retrieveData(const QString& mimeType, QMetaType type) const override;

private:
    QString binId_;
    QStringList itemIds_;
    mutable std::optional<QList<QUrl>> files_;
};

} // namespace lumenis::ui
