#include "lumenis/ui/capture.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QWidget>

// Screenshots and layout dumps for the cross-platform consistency check (Phase 1 gate 1, capture.h). The
// text-free picture works by making every text role of the palette transparent, which is why
// custom-painted widgets draw their text with palette colours (WindowText, BrightText). The pictures and
// dumps of each platform are compared by CI's ui-consistency job; scripts/ci-screenshots.py carries them
// between jobs.

namespace lumenis::ui {

namespace {

// One path element: "Class:objectName", or "Class[i]" where i counts earlier siblings of the same class.
QString segment(const QWidget* w) {
    const QString cls = QString::fromLatin1(w->metaObject()->className());
    if (!w->objectName().isEmpty()) {
        return cls + QLatin1Char(':') + w->objectName();
    }
    // Index among siblings of the same class.
    int index = 0;
    if (const QWidget* parent = w->parentWidget()) {
        for (const QObject* o : parent->children()) {
            if (o == w) break;
            if (o->isWidgetType() && o->metaObject() == w->metaObject()) ++index;
        }
    }
    return QStringLiteral("%1[%2]").arg(cls).arg(index);
}

// Appends `w` and, depth first, its visible children; separate windows (dialogs, popups) are left out.
void describe(const QWidget* root, const QWidget* w, const QString& path, QJsonArray& out) {
    const QPoint topLeft = w->mapTo(root, QPoint(0, 0));
    out.append(QJsonObject{{QStringLiteral("path"), path},
                           {QStringLiteral("x"), topLeft.x()},
                           {QStringLiteral("y"), topLeft.y()},
                           {QStringLiteral("w"), w->width()},
                           {QStringLiteral("h"), w->height()}});
    for (const QObject* o : w->children()) {
        if (!o->isWidgetType()) continue;
        const auto* child = static_cast<const QWidget*>(o);
        if (!child->isVisible() || child->isWindow()) continue;
        describe(root, child, path + QLatin1Char('/') + segment(child), out);
    }
}

// Applies `palette` to `root` and every descendant that carries its own
// palette (labels with custom colours, for example).
// Only the text roles are copied into the descendants, so their other colours stay as they are.
void applyPalette(QWidget* root, const QPalette& palette) {
    root->setPalette(palette);
    for (QWidget* w : root->findChildren<QWidget*>()) {
        QPalette p = w->palette();
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::BrightText,
                                QPalette::HighlightedText, QPalette::PlaceholderText, QPalette::ToolTipText}) {
            for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                p.setColor(group, role, palette.color(group, role));
            }
        }
        w->setPalette(p);
    }
}

} // namespace

QJsonArray describeLayout(const QWidget* root) {
    QJsonArray out;
    describe(root, root, segment(root), out);
    return out;
}

// Grabs the widget as rendered, writes the layout, then swaps in a palette with transparent text (for
// the application and the widget tree), grabs again and puts the palettes back.
bool capture(QWidget* widget, const QDir& dir, const QString& name) {
    bool ok = widget->grab().save(dir.filePath(name + QStringLiteral(".png")));

    QFile layout(dir.filePath(name + QStringLiteral(".layout.json")));
    ok = layout.open(QIODevice::WriteOnly) && layout.write(QJsonDocument(describeLayout(widget)).toJson()) > 0 && ok;
    layout.close();

    // Same picture with text invisible. Style sheets that name palette
    // roles (palette(highlighted-text)) follow the palette too.
    const QPalette original = widget->palette();
    // Start from the application palette: it has every role set, whereas a
    // widget's own palette may only carry the roles it overrides.
    QPalette noText = QApplication::palette();
    const QColor transparent(0, 0, 0, 0);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::BrightText,
                            QPalette::HighlightedText, QPalette::PlaceholderText, QPalette::ToolTipText}) {
        for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            noText.setColor(group, role, transparent);
        }
    }
    // Style sheets resolve palette(...) when the widget is polished, so
    // re-apply them after changing the palette.
    const QPalette appPalette = QApplication::palette();
    const auto repolish = [widget] {
        const QString sheet = widget->styleSheet();
        widget->setStyleSheet(QString());
        widget->setStyleSheet(sheet);
    };
    QApplication::setPalette(noText);
    applyPalette(widget, noText);
    repolish();
    ok = widget->grab().save(dir.filePath(name + QStringLiteral(".notext.png"))) && ok;
    QApplication::setPalette(appPalette);
    applyPalette(widget, original);
    repolish();
    return ok;
}

} // namespace lumenis::ui
