// Printing (printing.h; Editing Guide p.292, p.424, p.645): the documents File > Print Bin, Print Frame, Print Timeline
// and Print put on paper, and how their pages are laid out and painted on a printer, a PDF or images.
#include "lumenis/ui/printing.h"

#include "lumenis/model/bin.h"
#include "lumenis/ui/bin_view.h"

#include <QAbstractTextDocumentLayout>
#include <QHeaderView>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QTableView>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

// The bin's frames on paper (ours): four a row at 160 x 90 page pixels, about 1.7 inches.
constexpr int kFrameColumns = 4;
const QSize kPrintedFrame(160, 90);
// Room under a picture for its caption.
constexpr double kCaptionHeight = 28;

// Text is laid out as if on a 96-dpi screen, whatever the platform (macOS's default is 72), so the page-pixel scale of
// printing.h gives fonts their point sizes on paper.
QPaintDevice* layoutDevice() {
    static QImage device = [] {
        QImage image(1, 1, QImage::Format_RGB32);
        const int dotsPerMeter = static_cast<int>(std::lround(kPagePixelsPerInch / 0.0254));
        image.setDotsPerMeterX(dotsPerMeter);
        image.setDotsPerMeterY(dotsPerMeter);
        return image;
    }();
    return &device;
}

std::unique_ptr<QTextDocument> newText() {
    auto doc = std::make_unique<QTextDocument>();
    doc->documentLayout()->setPaintDevice(layoutDevice());
    QFont font(QStringLiteral("Helvetica"));
    font.setStyleHint(QFont::SansSerif);
    font.setPointSizeF(9);
    doc->setDefaultFont(font);
    doc->setDocumentMargin(0);
    return doc;
}

// The bin's name over its contents.
void insertHeading(QTextCursor& cursor, const QString& text) {
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    bold.setFontPointSize(12);
    cursor.insertText(text, bold);
    QTextBlockFormat after;
    after.setBottomMargin(8);
    cursor.insertBlock(after, QTextCharFormat());
}

QTextTableFormat tableFormat(int headerRows) {
    QTextTableFormat f;
    f.setHeaderRowCount(headerRows);
    f.setCellPadding(3);
    f.setCellSpacing(0);
    f.setBorder(0.5);
    f.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    f.setBorderBrush(QColor(160, 160, 160));
    f.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    return f;
}

// An item's picture for paper; a grey frame when it has none (offline, no media).
QImage printedFrame(const BinView& view, const QString& id) {
    QImage picture = view.frameView()->picture(id, kPrintedFrame, true);
    if (picture.isNull()) {
        picture = QImage(kPrintedFrame, QImage::Format_RGB32);
        picture.fill(QColor(90, 90, 90));
    }
    return picture;
}

void insertFrame(QTextDocument& doc, QTextCursor& cursor, const BinView& view, const QString& id) {
    const QUrl url(QStringLiteral("frame://%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(id))));
    doc.addResource(QTextDocument::ImageResource, url, printedFrame(view, id));
    QTextImageFormat image;
    image.setName(url.toString());
    image.setWidth(kPrintedFrame.width());
    image.setHeight(kPrintedFrame.height());
    cursor.insertImage(image);
}

std::unique_ptr<QTextDocument> tableText(const QString& title, const QTableView& view) {
    auto doc = newText();
    QTextCursor cursor(doc.get());
    insertHeading(cursor, title);
    const QTableView* table = &view;
    const QAbstractItemModel* model = table->model();
    const QHeaderView* header = table->horizontalHeader();
    // The shown columns in the order shown; a column with neither a heading nor text (the icons) is left out.
    QList<int> columns;
    for (int visual = 0; visual < header->count(); ++visual) {
        const int logical = header->logicalIndex(visual);
        if (logical < 0 || header->isSectionHidden(logical)) continue;
        bool any = !model->headerData(logical, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty();
        for (int row = 0; !any && row < model->rowCount(); ++row)
            any = !model->data(model->index(row, logical), Qt::DisplayRole).toString().isEmpty();
        if (any) columns << logical;
    }
    if (columns.isEmpty()) return doc;
    QTextTable* out = cursor.insertTable(model->rowCount() + 1, static_cast<int>(columns.size()), tableFormat(1));
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    for (int c = 0; c < columns.size(); ++c) {
        QTextCursor cell = out->cellAt(0, c).firstCursorPosition();
        cell.insertText(model->headerData(columns.at(c), Qt::Horizontal, Qt::DisplayRole).toString(), bold);
        QTextTableCellFormat shade;
        shade.setBackground(QColor(225, 225, 225));
        out->cellAt(0, c).setFormat(shade);
    }
    for (int row = 0; row < model->rowCount(); ++row) {
        for (int c = 0; c < columns.size(); ++c) {
            out->cellAt(row + 1, c).firstCursorPosition().insertText(
                model->data(model->index(row, columns.at(c)), Qt::DisplayRole).toString());
        }
    }
    return doc;
}

std::unique_ptr<QTextDocument> frameView(const BinView& view) {
    auto doc = newText();
    QTextCursor cursor(doc.get());
    insertHeading(cursor, view.document().name());
    // The order the frames read: rows top to bottom, each left to right (frames within half a frame of each other's
    // top share a row).
    QStringList ids = view.visibleIds();
    const BinFrameView* frames = view.frameView();
    const int rowSlack = std::max(1, BinFrameView::frameSize(0).height() / 2);
    std::stable_sort(ids.begin(), ids.end(), [&](const QString& a, const QString& b) {
        const QPoint pa = frames->framePosition(a);
        const QPoint pb = frames->framePosition(b);
        if (std::abs(pa.y() - pb.y()) > rowSlack) return pa.y() < pb.y();
        return pa.x() < pb.x();
    });
    if (ids.isEmpty()) return doc;
    const int rows = static_cast<int>((ids.size() + kFrameColumns - 1) / kFrameColumns);
    QTextTableFormat format = tableFormat(0);
    format.setBorder(0);
    format.setCellPadding(6);
    QTextTable* out = cursor.insertTable(rows, kFrameColumns, format);
    for (int i = 0; i < ids.size(); ++i) {
        QTextCursor cell = out->cellAt(i / kFrameColumns, i % kFrameColumns).firstCursorPosition();
        insertFrame(*doc, cell, view, ids.at(i));
        cell.insertBlock();
        const model::BinItem* item = view.document().bin().item(ids.at(i));
        cell.insertText(item ? item->name : QString());
    }
    return doc;
}

std::unique_ptr<QTextDocument> scriptView(const BinView& view) {
    auto doc = newText();
    QTextCursor cursor(doc.get());
    insertHeading(cursor, view.document().name());
    const QStringList ids = view.visibleIds();
    if (ids.isEmpty()) return doc;
    QTextTableFormat format = tableFormat(0);
    format.setColumnWidthConstraints({QTextLength(QTextLength::FixedLength, kPrintedFrame.width() + 12),
                                      QTextLength(QTextLength::VariableLength, 0)});
    QTextTable* out = cursor.insertTable(static_cast<int>(ids.size()), 2, format);
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    for (int i = 0; i < ids.size(); ++i) {
        QTextCursor picture = out->cellAt(i, 0).firstCursorPosition();
        insertFrame(*doc, picture, view, ids.at(i));
        QTextCursor text = out->cellAt(i, 1).firstCursorPosition();
        const model::BinItem* item = view.document().bin().item(ids.at(i));
        if (!item) continue;
        text.insertText(item->name, bold);
        // "This text does not appear in sequences edited from the clips, only in printouts of the bin in Script view."
        for (const QString& line : item->script.split(QLatin1Char('\n'))) {
            text.insertBlock(QTextBlockFormat(), QTextCharFormat());
            text.insertText(line, QTextCharFormat());
        }
    }
    return doc;
}

} // namespace

PrintDocument::PrintDocument() = default;
PrintDocument::~PrintDocument() = default;
PrintDocument::PrintDocument(PrintDocument&&) noexcept = default;
PrintDocument& PrintDocument::operator=(PrintDocument&&) noexcept = default;

PrintDocument PrintDocument::fromText(const QString& title, std::unique_ptr<QTextDocument> text) {
    PrintDocument d;
    d.title_ = title;
    d.text_ = std::move(text);
    return d;
}

PrintDocument PrintDocument::fromPicture(const QString& title, const QImage& picture, const QString& caption,
                                         double maxScale) {
    PrintDocument d;
    d.title_ = title;
    d.picture_ = picture;
    d.caption_ = caption;
    d.maxScale_ = maxScale;
    return d;
}

QString PrintDocument::text() const {
    return text_ ? text_->toPlainText() : caption_;
}

int PrintDocument::pageCount(const QSizeF& page) const {
    if (text_) {
        text_->setPageSize(page);
        return std::max(1, text_->pageCount());
    }
    return picture_.isNull() ? 0 : 1;
}

void PrintDocument::paintPage(QPainter& painter, int index, const QSizeF& page) const {
    painter.save();
    if (text_) {
        text_->setPageSize(page);
        // The page's slice of the laid-out text (QTextDocument stacks its pages one under the other).
        const QRectF slice(0, index * page.height(), page.width(), page.height());
        painter.translate(0, -slice.top());
        painter.setClipRect(slice);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = slice;
        context.palette.setColor(QPalette::Text, Qt::black);
        text_->documentLayout()->draw(&painter, context);
    } else if (!picture_.isNull() && index == 0) {
        const QSizeF size = picture_.deviceIndependentSize();
        const double room = caption_.isEmpty() ? page.height() : page.height() - kCaptionHeight;
        const double scale = std::min({maxScale_, page.width() / size.width(), room / size.height()});
        const QSizeF shown = size * scale;
        const QRectF target(QPointF((page.width() - shown.width()) / 2, 0), shown);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, picture_);
        if (!caption_.isEmpty()) {
            QFont font(QStringLiteral("Helvetica"));
            font.setStyleHint(QFont::SansSerif);
            // Page pixels, so the caption is 9 points on paper.
            font.setPixelSize(12);
            painter.setFont(font);
            painter.setPen(Qt::black);
            painter.drawText(QRectF(0, target.bottom() + 6, page.width(), kCaptionHeight - 6),
                             Qt::AlignHCenter | Qt::AlignTop, caption_);
        }
    }
    painter.restore();
}

QSizeF printablePageSize(const QPageLayout& layout) {
    const QRectF inches = layout.paintRect(QPageLayout::Inch);
    return inches.size() * kPagePixelsPerInch;
}

namespace {

// Paints every page on `painter`, whose device has `dpi` and a page `page` (page pixels) at its origin; `next` starts
// a new page.
template <typename NewPage>
void paintPages(const PrintDocument& document, QPainter& painter, double dpi, const QSizeF& page, NewPage next) {
    const int pages = document.pageCount(page);
    const double scale = dpi / kPagePixelsPerInch;
    for (int i = 0; i < pages; ++i) {
        if (i > 0 && !next(i)) return;
        painter.save();
        painter.scale(scale, scale);
        document.paintPage(painter, i, page);
        painter.restore();
    }
}

} // namespace

bool printDocument(const PrintDocument& document, QPagedPaintDevice& device) {
    if (document.isEmpty()) return false;
    QPainter painter;
    if (!painter.begin(&device)) return false;
    const QSizeF page = printablePageSize(device.pageLayout());
    // A printer's and a PDF's painter start at the printable area's corner.
    paintPages(document, painter, device.logicalDpiX(), page, [&device](int) { return device.newPage(); });
    painter.end();
    return true;
}

QList<QImage> renderPages(const PrintDocument& document, const QPageLayout& layout, int dpi) {
    QList<QImage> out;
    if (document.isEmpty()) return out;
    const QSizeF page = printablePageSize(layout);
    const QRect full = layout.fullRectPixels(dpi);
    const QRect paint = layout.paintRectPixels(dpi);
    const int pages = document.pageCount(page);
    const double scale = dpi / kPagePixelsPerInch;
    for (int i = 0; i < pages; ++i) {
        QImage image(full.size(), QImage::Format_RGB32);
        image.fill(Qt::white);
        const int dotsPerMeter = static_cast<int>(std::lround(dpi / 0.0254));
        image.setDotsPerMeterX(dotsPerMeter);
        image.setDotsPerMeterY(dotsPerMeter);
        QPainter painter(&image);
        painter.translate(paint.topLeft());
        painter.scale(scale, scale);
        document.paintPage(painter, i, page);
        painter.end();
        out << image;
    }
    return out;
}

PrintDocument binDocument(const BinView& view) {
    std::unique_ptr<QTextDocument> text;
    switch (view.mode()) {
    case model::BinViewMode::Text: text = tableText(view.document().name(), *view.table()); break;
    case model::BinViewMode::Frame: text = frameView(view); break;
    case model::BinViewMode::Script: text = scriptView(view); break;
    }
    return PrintDocument::fromText(view.document().name(), std::move(text));
}

PrintDocument tableDocument(const QString& title, const QTableView& table) {
    return PrintDocument::fromText(title, tableText(title, table));
}

PrintDocument consoleDocument(const QString& log) {
    auto doc = newText();
    QFont font(QStringLiteral("Courier"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSizeF(9);
    doc->setDefaultFont(font);
    doc->setPlainText(log);
    return PrintDocument::fromText(QObject::tr("Console"), std::move(doc));
}

} // namespace lumenis::ui
