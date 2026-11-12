// Accessible names from what a control shows (accessibility.h; ROADMAP 10.3, docs/spec/accessibility.md).

#include "lumenis/ui/accessibility.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QTextDocumentFragment>

namespace lumenis::ui {

namespace {

// Names we gave are marked, so they can follow the control's tooltip; a name set by the code itself is never touched.
constexpr char kDerived[] = "lumenisDerivedAccessibleName";

QString plain(const QString& text) {
    // Tooltips may be rich text ("<b>Overwrite</b> (B)"); a reader wants the words.
    QString out = Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
    out.remove(QLatin1Char('&')); // mnemonics
    return out.simplified();
}

class AccessibleNames : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::Polish:
        case QEvent::Show:
        case QEvent::ToolTipChange:
            if (watched->isWidgetType()) name(static_cast<QWidget*>(watched));
            break;
        default:
            break;
        }
        return false;
    }

private:
    static void name(QWidget* w) {
        const bool ours = w->property(kDerived).toBool();
        if (!w->accessibleName().isEmpty() && !ours) return; // set by the code: left alone
        const QString derived = derivedAccessibleName(w);
        if (derived.isEmpty() || derived == w->accessibleName()) return;
        w->setAccessibleName(derived);
        w->setProperty(kDerived, true);
    }
};

} // namespace

QString derivedAccessibleName(const QWidget* w) {
    if (const auto* b = qobject_cast<const QAbstractButton*>(w)) {
        const QString text = plain(b->text());
        if (!text.isEmpty()) return {}; // Qt names it by its text already
    }
    const QString tip = plain(w->toolTip());
    if (!tip.isEmpty()) return tip;
    // The clear button Qt puts in a line edit (setClearButtonEnabled) has neither text nor tooltip.
    if (qstrcmp(w->metaObject()->className(), "QLineEditIconButton") == 0) return QApplication::translate("Accessibility", "Clear");
    if (const auto* e = qobject_cast<const QLineEdit*>(w)) {
        // A spin box's own text field is named as the spin box is.
        if (const auto* spin = qobject_cast<const QAbstractSpinBox*>(w->parentWidget())) {
            if (!spin->accessibleName().isEmpty()) return spin->accessibleName();
            const QString derived = derivedAccessibleName(spin);
            if (!derived.isEmpty()) return derived;
            // Named by a form's label (its buddy), which only Qt's own interface for the spin box knows.
            if (QAccessibleInterface* a = QAccessible::queryAccessibleInterface(const_cast<QAbstractSpinBox*>(spin)))
                return plain(a->text(QAccessible::Name));
            return {};
        }
        return plain(e->placeholderText());
    }
    return {};
}

void installAccessibleNames(QApplication& app) {
    // Once per application. The filter is the application's child and goes with it; the pointer is cleared then, so a
    // later application gets its own.
    static AccessibleNames* filter = nullptr;
    if (filter) return;
    filter = new AccessibleNames(&app);
    QObject::connect(filter, &QObject::destroyed, [] { filter = nullptr; });
    app.installEventFilter(filter);
}

} // namespace lumenis::ui
