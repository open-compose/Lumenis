#include "lumenis/ui/clip_color_dialog.h"

#include "lumenis/model/bin.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

// The Clip Color dialog box and the clip colour menu (clip_color_dialog.h).
namespace lumenis::ui {

namespace {

// A colour square for menus and swatch buttons; None is crossed out.
QIcon swatchIcon(const QColor& color) {
    QPixmap pm(14, 14);
    pm.fill(color.isValid() ? color : QColor(Qt::transparent));
    QPainter p(&pm);
    p.setPen(QColor(20, 20, 20));
    p.drawRect(0, 0, 13, 13);
    if (!color.isValid()) p.drawLine(0, 13, 13, 0);
    return QIcon(pm);
}

} // namespace

QMenu* addClipColorMenu(QMenu* parent, const QString& title, const std::function<void(const QString&)>& apply,
                        const QString& noneText) {
    QMenu* menu = parent->addMenu(title);
    const QString none = noneText.isEmpty() ? QObject::tr("None") : noneText;
    QObject::connect(menu->addAction(swatchIcon(QColor()), none), &QAction::triggered, menu,
                     [apply] { apply(QString()); });
    for (const model::ClipColor& c : model::clipColors()) {
        QAction* a = menu->addAction(swatchIcon(QColor(c.hex)), c.name);
        a->setObjectName(QStringLiteral("color:") + c.name);
        QObject::connect(a, &QAction::triggered, menu, [apply, hex = c.hex] { apply(hex); });
    }
    menu->addSeparator();
    QObject::connect(menu->addAction(QObject::tr("Other...")), &QAction::triggered, menu, [menu, apply] {
        const QColor c = QColorDialog::getColor(Qt::white, menu->parentWidget());
        if (c.isValid()) apply(c.name());
    });
    return menu;
}

// One row for each option in the guide's order (p.595-596); Mixed Rates has a swatch for each rate
// on a row of its own below it.
ClipColorDialog::ClipColorDialog(const model::ClipColorOptions& options, QWidget* parent)
    : QDialog(parent), options_(options) {
    setObjectName(QStringLiteral("clipColorDialog"));
    setWindowTitle(tr("Clip Color"));
    auto* grid = new QGridLayout;
    int row = 0;
    auto add = [&](const QString& key, const QString& text, bool on, bool enabled, const QString& colorKey,
                   const QString& hex) {
        auto* box = new QCheckBox(text, this);
        box->setObjectName(QStringLiteral("clipColor:") + key);
        box->setChecked(on);
        box->setEnabled(enabled);
        checks_.insert(key, box);
        grid->addWidget(box, row, 0);
        if (!colorKey.isEmpty()) grid->addWidget(makeSwatch(colorKey, hex), row, 1);
        ++row;
        return box;
    };
    // "This option is only available if you are working in a MultiRez environment" (p.595): ours, with Dynamic Relink
    // on (setMultiRez). Its swatches: mismatched and not available in the target settings (p.596, p.1113).
    QCheckBox* resolution = add(QStringLiteral("resolution"), tr("Resolution Tracking"), options.resolution, false,
                                QStringLiteral("mismatchColor"), options.mismatchColor);
    resolution->setToolTip(tr("Only in a Multi-Resolution environment"));
    grid->addWidget(makeSwatch(QStringLiteral("unavailableColor"), options.unavailableColor), row - 1, 2);
    add(QStringLiteral("offline"), tr("Offline"), options.offline, true, QStringLiteral("offlineColor"), options.offlineColor);
    add(QStringLiteral("proxy"), tr("Proxy"), options.proxy, false, {}, {})->setToolTip(tr("Comes with proxy media"));
    add(QStringLiteral("linked"), tr("Linked Clips"), options.linked, true, QStringLiteral("linkedColor"), options.linkedColor);
    add(QStringLiteral("mixedRates"), tr("Mixed Rates"), options.mixedRates, true, {}, {});
    auto* rates = new QHBoxLayout;
    for (const QString& rate : model::mixedRates()) {
        auto* label = new QLabel(rate, this);
        rates->addWidget(label);
        rates->addWidget(makeSwatch(rate, options.rateColors.value(rate)));
    }
    rates->addStretch(1);
    grid->addLayout(rates, row++, 0, 1, 2);
    add(QStringLiteral("sdHd"), tr("SD/HD"), options.sdHd, true, QStringLiteral("sdHdColor"), options.sdHdColor);
    add(QStringLiteral("local"), tr("Timeline Local"), options.local, true, {}, {});
    add(QStringLiteral("source"), tr("Source"), options.source, true, {}, {});

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* defaults = buttons->addButton(tr("Default Colors"), QDialogButtonBox::ResetRole);
    defaults->setObjectName(QStringLiteral("defaultColors"));
    connect(defaults, &QPushButton::clicked, this, &ClipColorDialog::defaultColors);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(grid);
    layout->addWidget(buttons);
}

// "Click the color swatch for the option you want to change. A color picker grid opens" (p.596).
QToolButton* ClipColorDialog::makeSwatch(const QString& key, const QString& hex) {
    auto* b = new QToolButton(this);
    b->setObjectName(QStringLiteral("swatch:") + key);
    b->setIcon(swatchIcon(QColor(hex)));
    b->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(b);
    // The grid: the palette's colours in rows of seven.
    auto* holder = new QWidget(menu);
    auto* g = new QGridLayout(holder);
    g->setContentsMargins(4, 4, 4, 4);
    g->setSpacing(2);
    int i = 0;
    for (const model::ClipColor& c : model::clipColors()) {
        auto* cell = new QToolButton(holder);
        cell->setIcon(swatchIcon(QColor(c.hex)));
        cell->setToolTip(c.name);
        cell->setAutoRaise(true);
        connect(cell, &QToolButton::clicked, this, [this, menu, key, hex = c.hex] {
            setColor(key, hex);
            menu->close();
        });
        g->addWidget(cell, i / 7, i % 7);
        ++i;
    }
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(holder);
    menu->addAction(action);
    b->setMenu(menu);
    swatches_.insert(key, b);
    return b;
}

void ClipColorDialog::setColor(const QString& key, const QString& hex) {
    if (key == QLatin1String("offlineColor")) {
        options_.offlineColor = hex;
    } else if (key == QLatin1String("linkedColor")) {
        options_.linkedColor = hex;
    } else if (key == QLatin1String("sdHdColor")) {
        options_.sdHdColor = hex;
    } else if (key == QLatin1String("mismatchColor")) {
        options_.mismatchColor = hex;
    } else if (key == QLatin1String("unavailableColor")) {
        options_.unavailableColor = hex;
    } else if (options_.rateColors.contains(key)) {
        options_.rateColors[key] = hex;
    } else {
        return;
    }
    if (QToolButton* b = swatches_.value(key)) b->setIcon(swatchIcon(QColor(hex)));
}

// "The Offline, Mixed Rates, and SD/HD color swatches reset to their default colors" (p.597); the
// Linked Clips and Resolution Tracking swatches too (ours; p.596 lists Resolution Tracking among the colours reset).
void ClipColorDialog::setMultiRez(bool on) {
    if (QCheckBox* r = checks_.value(QStringLiteral("resolution"))) r->setEnabled(on);
}

void ClipColorDialog::defaultColors() {
    const model::ClipColorOptions d = model::ClipColorOptions::defaults();
    setColor(QStringLiteral("offlineColor"), d.offlineColor);
    setColor(QStringLiteral("linkedColor"), d.linkedColor);
    setColor(QStringLiteral("sdHdColor"), d.sdHdColor);
    setColor(QStringLiteral("mismatchColor"), d.mismatchColor);
    setColor(QStringLiteral("unavailableColor"), d.unavailableColor);
    for (auto it = d.rateColors.begin(); it != d.rateColors.end(); ++it) setColor(it.key(), it.value());
}

model::ClipColorOptions ClipColorDialog::options() const {
    model::ClipColorOptions o = options_;
    if (QCheckBox* r = checks_.value(QStringLiteral("resolution")); r && r->isEnabled()) o.resolution = r->isChecked();
    o.offline = checks_.value(QStringLiteral("offline"))->isChecked();
    o.linked = checks_.value(QStringLiteral("linked"))->isChecked();
    o.mixedRates = checks_.value(QStringLiteral("mixedRates"))->isChecked();
    o.sdHd = checks_.value(QStringLiteral("sdHd"))->isChecked();
    o.local = checks_.value(QStringLiteral("local"))->isChecked();
    o.source = checks_.value(QStringLiteral("source"))->isChecked();
    return o;
}

} // namespace lumenis::ui
