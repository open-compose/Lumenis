#include "lumenis/ui/timecode_window.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QVBoxLayout>

// The Timecode window (timecode_window.h; Editing Guide p.388-389). It reads the monitors' tracking
// values from the Composer (model::MonitorState::tracking) whenever the Composer changes; it changes
// nothing in them.

namespace lumenis::ui {

namespace {

constexpr QRgb kTimecodeGreen = 0xff4cd98c; // the value, green as in the picture on p.389
constexpr QRgb kWindowBlack = 0xff141414;

// "Mas", "Dur", "I/O", "Abs", "Rem", "Nam" (ours: the picture on p.389 shows "Mas" for Master).
QString abbreviation(model::TrackingFormat format) {
    switch (format) {
    case model::TrackingFormat::InOut: return QStringLiteral("I/O");
    case model::TrackingFormat::ClipName: return QStringLiteral("Nam");
    case model::TrackingFormat::None: return {};
    default: return model::trackingLabel(format).left(3);
    }
}

} // namespace

TimecodeWindow::TimecodeWindow(Composer& composer, QWidget* parent) : QWidget(parent, Qt::Tool), composer_(composer) {
    setObjectName(QStringLiteral("timecodeWindow"));
    setWindowTitle(tr("Timecode"));
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(kWindowBlack));
    setPalette(pal);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(8, 6, 8, 6);
    layout_->setSpacing(2);
    // One line to start with: the sequence's master timecode, as in the picture on p.389.
    lines_ = {Line{MonitorId::Record, model::TrackingFormat::Master}};
    rebuild();
    connect(&composer_, &Composer::changed, this, &TimecodeWindow::refresh);
    resize(280, 60);
}

const QList<int>& TimecodeWindow::fontSizes() {
    static const QList<int> sizes{9, 10, 12, 14, 18, 24, 36, 48};
    return sizes;
}

void TimecodeWindow::setLine(int index, const Line& line) {
    if (index < 0 || index >= lines_.size()) return;
    lines_[index] = line;
    refresh();
}

bool TimecodeWindow::addLine() {
    if (lines_.size() >= kMaxLines) return false;
    lines_ << Line{MonitorId::Record, model::TrackingFormat::None};
    rebuild();
    adjustSize();
    return true;
}

void TimecodeWindow::setFontSize(int points) {
    fontSize_ = points;
    rebuild();
    adjustSize();
}

QString TimecodeWindow::labelText(int index) const {
    const Line& line = lines_.value(index);
    if (line.format == model::TrackingFormat::None) return {};
    // Ours: source lines say so.
    return line.monitor == MonitorId::Source ? QStringLiteral("S ") + abbreviation(line.format) : abbreviation(line.format);
}

QString TimecodeWindow::valueText(int index) const {
    const Line& line = lines_.value(index);
    if (line.format == model::TrackingFormat::None) return {};
    const model::MonitorState& s = composer_.state(line.monitor);
    return s.isEmpty() ? QString() : s.tracking(line.format);
}

// A row a line: the label in the window's text colour, the value in green (palette colours, so
// text-free screenshots hide them).
void TimecodeWindow::rebuild() {
    for (QWidget* row : rows_) delete row;
    rows_.clear();
    QFont font = this->font();
    font.setPointSize(fontSize_);
    for (int i = 0; i < lines_.size(); ++i) {
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(row);
        label->setObjectName(QStringLiteral("label"));
        label->setFont(font);
        auto* value = new QLabel(row);
        value->setObjectName(QStringLiteral("value"));
        value->setFont(font);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QPalette pal = value->palette();
        pal.setColor(QPalette::WindowText, QColor(kTimecodeGreen));
        value->setPalette(pal);
        h->addWidget(label);
        h->addStretch(1);
        h->addWidget(value);
        layout_->addWidget(row);
        rows_ << row;
    }
    refresh();
}

void TimecodeWindow::refresh() {
    for (int i = 0; i < rows_.size(); ++i) {
        rows_.at(i)->findChild<QLabel*>(QStringLiteral("label"))->setText(labelText(i));
        rows_.at(i)->findChild<QLabel*>(QStringLiteral("value"))->setText(valueText(i));
    }
}

int TimecodeWindow::lineAt(const QPoint& pos) const {
    for (int i = 0; i < rows_.size(); ++i) {
        if (rows_.at(i)->geometry().adjusted(0, -1, 0, 1).contains(pos)) return i;
    }
    return static_cast<int>(rows_.size()) - 1;
}

// "Click in the Timecode window, and select an option" (p.389).
void TimecodeWindow::mousePressEvent(QMouseEvent* event) {
    QMenu* menu = menuFor(lineAt(event->position().toPoint()));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPosition().toPoint());
}

// Sequence and Source with their formats, None; Add Line, Size; then the sequence's values, as in the
// picture on p.389.
QMenu* TimecodeWindow::menuFor(int index) {
    auto* menu = new QMenu(this);
    const Line current = lines_.value(index);
    for (const auto& [monitor, title] :
         {std::pair{MonitorId::Record, tr("Sequence")}, std::pair{MonitorId::Source, tr("Source")}}) {
        QMenu* sub = menu->addMenu(title);
        sub->setEnabled(!composer_.state(monitor).isEmpty());
        for (model::TrackingFormat f : model::trackingFormats()) {
            if (f == model::TrackingFormat::None) continue;
            QAction* a = sub->addAction(model::trackingLabel(f));
            a->setCheckable(true);
            a->setChecked(current.monitor == monitor && current.format == f);
            connect(a, &QAction::triggered, this, [this, index, monitor = monitor, f] { setLine(index, Line{monitor, f}); });
        }
    }
    QAction* none = menu->addAction(tr("None"));
    connect(none, &QAction::triggered, this, [this, index] { setLine(index, Line{MonitorId::Record, model::TrackingFormat::None}); });
    menu->addSeparator();
    QAction* add = menu->addAction(tr("Add Line"));
    add->setEnabled(lines_.size() < kMaxLines);
    connect(add, &QAction::triggered, this, [this] { addLine(); });
    QMenu* size = menu->addMenu(tr("Size"));
    for (int points : fontSizes()) {
        QAction* a = size->addAction(QString::number(points));
        a->setCheckable(true);
        a->setChecked(points == fontSize_);
        connect(a, &QAction::triggered, this, [this, points] { setFontSize(points); });
    }
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (!record.isEmpty()) {
        menu->addSeparator();
        menu->addAction(tr("Sequence Timecode :"))->setEnabled(false);
        for (model::TrackingFormat f : model::trackingFormats()) {
            if (f == model::TrackingFormat::None || f == model::TrackingFormat::ClipName) continue;
            menu->addAction(QStringLiteral("%1\t%2").arg(model::trackingLabel(f), record.tracking(f)))->setEnabled(false);
        }
    }
    return menu;
}

} // namespace lumenis::ui
