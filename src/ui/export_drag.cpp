#include "lumenis/ui/export_drag.h"

#include <QApplication>
#include <QCursor>
#include <QWidget>

// The drag data behind exporting by drag and drop (export_drag.h; 11.4b). Qt asks retrieveData() for text/uri-list
// when a drop target reads it: on Windows when Explorer takes the CF_HDROP at the drop, on X11 and Wayland when the
// file manager reads the selection. So the export runs then, once, into the General setting's Temporary File Directory
// (p.869, p.1192), and the file manager copies the files where they were dropped.
namespace lumenis::ui {

namespace {

ExportDragHandler& handler() {
    static ExportDragHandler h;
    return h;
}

const QString kUriList = QStringLiteral("text/uri-list");

} // namespace

void setExportDragHandler(ExportDragHandler h) {
    handler() = std::move(h);
}

ExportDragData::ExportDragData(const QString& binId, const QStringList& itemIds) : binId_(binId), itemIds_(itemIds) {}

QStringList ExportDragData::formats() const {
    QStringList f = QMimeData::formats();
    if (handler() && !f.contains(kUriList)) f << kUriList;
    return f;
}

bool ExportDragData::hasFormat(const QString& mimeType) const {
    if (mimeType == kUriList && handler()) return true;
    return QMimeData::hasFormat(mimeType);
}

QList<QUrl> ExportDragData::exportedFiles() const {
    if (!handler() || itemIds_.isEmpty()) return {};
    if (!files_) files_ = handler()(binId_, itemIds_).value_or(QList<QUrl>{});
    return *files_;
}

// Our own windows never get the files: a drop on a bin, monitor or the Timeline is an edit or a move, not an export.
QVariant ExportDragData::retrieveData(const QString& mimeType, QMetaType type) const {
    if (mimeType == kUriList && !QMimeData::hasFormat(kUriList)) {
        if (!files_ && QApplication::widgetAt(QCursor::pos())) return QVariantList();
        QVariantList urls;
        for (const QUrl& u : exportedFiles()) urls << u;
        return urls;
    }
    return QMimeData::retrieveData(mimeType, type);
}

} // namespace lumenis::ui
