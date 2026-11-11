#pragma once

#include <QImage>
#include <QList>
#include <QPageLayout>
#include <QSizeF>
#include <QString>

#include <memory>

class QPagedPaintDevice;
class QPainter;
class QTableView;
class QTextDocument;

// Printing (9.1; docs/spec/printing.md; Editing Guide p.292 "Printing Bins", p.645 "Printing the Timeline", p.424
// "Printing the Contents of the Markers Window"). What File > Print Bin, Print Frame, Print Timeline and, for the Markers
// window and the Console, Print put on paper, as a PrintDocument: pages laid out for a page size and painted onto any
// paged device (a QPrinter after the Print dialog, a QPdfWriter, or images for tests and checks).
//
// Pages are laid out in "page pixels" of 96 a inch, the resolution text is laid out at on screen, so fonts keep their
// sizes; paintPage() scales them to the device.
namespace lumenis::ui {

class BinView;

class PrintDocument {
public:
    // A text document (a bin's Text, Frame or Script view as a table, the Console's log), paginated by Qt's text layout;
    // a table's heading rows repeat on every page.
    static PrintDocument fromText(const QString& title, std::unique_ptr<QTextDocument> text);
    // One picture (a frame, the Timeline, the Markers window) scaled to fit one page, never enlarged by more than
    // `maxScale`, with `caption` (may be empty) under it.
    static PrintDocument fromPicture(const QString& title, const QImage& picture, const QString& caption = QString(),
                                     double maxScale = 1.0);

    PrintDocument();
    ~PrintDocument();
    PrintDocument(PrintDocument&&) noexcept;
    PrintDocument& operator=(PrintDocument&&) noexcept;

    // The print job's name (the print queue shows it).
    const QString& title() const { return title_; }
    bool isEmpty() const { return !text_ && picture_.isNull(); }
    // The words on the pages, in order (a picture's caption); for tests.
    QString text() const;

    // Pages for a printable area of `page` (in page pixels). At least one when not empty.
    int pageCount(const QSizeF& page) const;
    // Paints page `index` (0-based) at the painter's origin in an area of `page` page pixels; the painter is already
    // scaled to page pixels.
    void paintPage(QPainter& painter, int index, const QSizeF& page) const;

private:
    QString title_;
    std::unique_ptr<QTextDocument> text_;
    QImage picture_;
    QString caption_;
    double maxScale_ = 1.0;
};

// Page pixels a inch (see above).
inline constexpr double kPagePixelsPerInch = 96.0;

// The printable area of `layout` in page pixels.
QSizeF printablePageSize(const QPageLayout& layout);

// Prints every page of `document` on `device` (its page layout and resolution). False if the painter cannot start (no
// printer) or the document is empty.
bool printDocument(const PrintDocument& document, QPagedPaintDevice& device);

// The pages as white images at `dpi` for `layout`'s full page, the printable area inside its margins: what a printer
// would put on paper. For tests and for the checks of BUILDING.md.
QList<QImage> renderPages(const PrintDocument& document, const QPageLayout& layout, int dpi = 72);

// The documents.
// File > Print Bin (p.292: "Select the Text, Script, or Frame bin view of the bin you want to print"): the bin's name and
// what its current view shows.
// * Text view: a table of the shown columns in the view's order, the rows sorted and sifted as shown.
// * Frame view: the frames with their names, in rows, in the order the frames read (top to bottom, left to right), so a
//   storyboard (p.288: "To print the storyboard, select File > Print Bin") prints as it is laid out.
// * Script view: each item's frame, name and the text of its script box (p.253).
PrintDocument binDocument(const BinView& view);
// A table as it is shown (its shown columns in their order, its rows), under `title`: the Text view's printout, and the
// Media Tool's ("You can print Media Tool data by using the same procedures for printing bins", p.345).
PrintDocument tableDocument(const QString& title, const QTableView& table);
// The Console's log (p.424: marker information placed in the Console, then File > Print), as plain text.
PrintDocument consoleDocument(const QString& log);

} // namespace lumenis::ui
