// The Video Output tool (lumenis/ui/video_output_tool.h).
#include "lumenis/ui/video_output_tool.h"

#include "lumenis/media/test_patterns.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

QStringList VideoOutputTool::sdSliders() {
    // Luminance (Black is setup, Y Gain the white level, p.917), the colour sliders (Hue and Sat for composite or
    // S-Video, RY Gain and BY Gain for component, p.915) and the phase controls (p.917).
    return {QStringLiteral("Black"),  QStringLiteral("Y Gain"),  QStringLiteral("Hue"),      QStringLiteral("Sat"),
            QStringLiteral("RY Gain"), QStringLiteral("BY Gain"), QStringLiteral("H Phase"), QStringLiteral("SC Phase"),
            QStringLiteral("System Phase")};
}

QStringList VideoOutputTool::hdSliders() {
    // "Pr Gain and Pb Gain sliders (HD component YPbPr output)" (p.915).
    return {QStringLiteral("HD Black"), QStringLiteral("HD Y Gain"), QStringLiteral("Pr Gain"), QStringLiteral("Pb Gain")};
}

VideoOutputTool::VideoOutputTool(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("videoOutputTool"));
    auto* layout = new QVBoxLayout(this);
    stack_ = new QStackedWidget(this);
    layout->addWidget(stack_);
    // "If you are not using Avid input/output hardware (software-only), the Video Output tool is not available" (p.912).
    unavailable_ = new QLabel(tr("The Video Output tool needs video output hardware; none is connected."), this);
    unavailable_->setObjectName(QStringLiteral("voUnavailable"));
    unavailable_->setWordWrap(true);
    unavailable_->setAlignment(Qt::AlignCenter);
    stack_->addWidget(unavailable_);

    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    auto* header = new QHBoxLayout;
    deviceName_ = new QLabel(page);
    deviceName_->setObjectName(QStringLiteral("voDevice"));
    auto* settings = new QToolButton(page);
    settings->setObjectName(QStringLiteral("voSettings"));
    settings->setText(tr("Settings"));
    settings->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(settings);
    menu->addAction(tr("Save As..."), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save As"), tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (ok && !name.isEmpty()) emit saveAsRequested(name);
    });
    settings->setMenu(menu);
    header->addWidget(deviceName_, 1);
    header->addWidget(settings);
    pageLayout->addLayout(header);
    tabs_ = new QTabWidget(page);
    tabs_->setObjectName(QStringLiteral("voTabs"));
    pageLayout->addWidget(tabs_);

    // The Options tab (p.1225-1227).
    auto* options = new QWidget(tabs_);
    auto* form = new QFormLayout(options);
    auto combo = [&](const char* name, const QStringList& items) {
        auto* c = new QComboBox(options);
        c->setObjectName(QString::fromLatin1(name));
        c->addItems(items);
        connect(c, &QComboBox::currentIndexChanged, this, [this] { changed(); });
        return c;
    };
    sync_ = combo("voSyncLock", {});
    hdmiColor_ = combo("voHdmiColorSpace", {QStringLiteral("YCbCr"), QStringLiteral("RGB")});
    hdmiFormat_ = combo("voHdmiFormat", {QStringLiteral("SDAuto"), QStringLiteral("SDInterlaced"), QStringLiteral("SDProgressive"), QStringLiteral("HD")});
    crossconvert_ = combo("voCrossconvert", {QStringLiteral("OFF")});
    downconvert_ = combo("voDownconvert", {QStringLiteral("OFF"), QStringLiteral("Anamorphic"), QStringLiteral("Letterbox"), QStringLiteral("Center Cut")});
    componentFormat_ = combo("voComponentFormat", {QStringLiteral("SD Interlaced"), QStringLiteral("HD")});
    vbi_ = combo("voVbi", {QStringLiteral("Blank"), QStringLiteral("Preserve")});
    testPatterns_ = combo("voTestPatterns", QStringList{tr("None")} + media::testPatternNames());
    form->addRow(tr("Sync Lock:"), sync_);
    form->addRow(tr("HDMI Color Space:"), hdmiColor_);
    form->addRow(tr("HDMI Format:"), hdmiFormat_);
    form->addRow(tr("Crossconvert:"), crossconvert_);
    form->addRow(tr("Downconvert:"), downconvert_);
    form->addRow(tr("Component Format:"), componentFormat_);
    form->addRow(tr("VBI:"), vbi_);
    form->addRow(tr("Test Patterns:"), testPatterns_);
    tabs_->addTab(options, tr("Options"));
    sdOutput_ = new QComboBox(tabs_);
    sdOutput_->setObjectName(QStringLiteral("voSdOutput"));
    connect(sdOutput_, &QComboBox::currentIndexChanged, this, [this] { changed(); });
    hdOutput_ = new QComboBox(tabs_);
    hdOutput_->setObjectName(QStringLiteral("voHdOutput"));
    connect(hdOutput_, &QComboBox::currentIndexChanged, this, [this] { changed(); });
    tabs_->addTab(calibrationTab(sdSliders(), sdOutput_), tr("SD Cal"));
    tabs_->addTab(calibrationTab(hdSliders(), hdOutput_), tr("HD Cal"));
    stack_->addWidget(page);
    stack_->setCurrentWidget(unavailable_);
}

VideoOutputTool::~VideoOutputTool() = default;

QWidget* VideoOutputTool::calibrationTab(const QStringList& names, QComboBox* output) {
    auto* tab = new QWidget(tabs_);
    auto* grid = new QGridLayout(tab);
    grid->addWidget(new QLabel(tr("Output:"), tab), 0, 0);
    output->setParent(tab);
    grid->addWidget(output, 0, 1, 1, 2);
    int row = 1;
    for (const QString& name : names) {
        Slider s;
        const QString id = QString(name).remove(QLatin1Char(' '));
        // The preset button, lit green while the factory level is loaded (p.913).
        s.preset = new QPushButton(tab);
        s.preset->setObjectName(QStringLiteral("voPreset_") + id);
        s.preset->setCheckable(true);
        s.preset->setChecked(true);
        s.preset->setFixedSize(18, 18);
        s.preset->setStyleSheet(QStringLiteral("QPushButton { background: #555; border-radius: 9px; } QPushButton:checked { background: #3c3; }"));
        s.slider = new QSlider(Qt::Horizontal, tab);
        s.slider->setObjectName(QStringLiteral("voSlider_") + id);
        s.slider->setRange(-100, 100);
        s.value = new QLabel(QStringLiteral("0"), tab);
        s.value->setMinimumWidth(32);
        grid->addWidget(new QLabel(name, tab), row, 0);
        grid->addWidget(s.preset, row, 1);
        grid->addWidget(s.slider, row, 2);
        grid->addWidget(s.value, row, 3);
        connect(s.slider, &QSlider::valueChanged, this, [this, name](int v) {
            if (filling_) return;
            moveSlider(name, v);
        });
        connect(s.preset, &QPushButton::clicked, this, [this, name] {
            if (filling_) return;
            clickPreset(name);
        });
        sliders_.insert(name, s);
        ++row;
    }
    grid->setRowStretch(row, 1);
    return tab;
}

void VideoOutputTool::setDevices(std::vector<std::shared_ptr<ext::VideoOutputProvider>> providers) {
    session_.reset();
    for (const auto& p : providers) {
        if (!p) continue;
        for (const ext::VideoOutputDeviceInfo& d : p->devices()) {
            std::string why;
            session_ = p->open(d.id, &why);
            if (session_) {
                device_ = d;
                break;
            }
        }
        if (session_) break;
    }
    if (!session_) {
        stack_->setCurrentWidget(unavailable_);
        return;
    }
    filling_ = true;
    deviceName_->setText(QString::fromStdString(device_.name));
    auto fill = [](QComboBox* c, const std::vector<std::string>& items) {
        const QString keep = c->currentText();
        c->clear();
        for (const std::string& i : items) c->addItem(QString::fromStdString(i));
        const int at = c->findText(keep);
        if (at >= 0) c->setCurrentIndex(at);
    };
    fill(sync_, device_.syncSources);
    fill(sdOutput_, device_.sdOutputs);
    fill(hdOutput_, device_.hdOutputs);
    hdmiColor_->setEnabled(device_.hdmi);
    hdmiFormat_->setEnabled(device_.hdmi);
    filling_ = false;
    stack_->setCurrentIndex(1);
    sendAll();
}

void VideoOutputTool::setValues(const QJsonObject& v) {
    filling_ = true;
    auto pick = [&](QComboBox* c, const char* key) {
        const int i = c->findText(v.value(QLatin1String(key)).toString());
        if (i >= 0) c->setCurrentIndex(i);
    };
    pick(sync_, "syncLock");
    pick(sdOutput_, "sdOutput");
    pick(hdOutput_, "hdOutput");
    pick(hdmiColor_, "hdmiColorSpace");
    pick(hdmiFormat_, "hdmiFormat");
    pick(crossconvert_, "crossconvert");
    pick(downconvert_, "downconvert");
    pick(componentFormat_, "componentFormat");
    pick(vbi_, "vbi");
    const QJsonObject cal = v.value(QStringLiteral("calibration")).toObject();
    for (auto it = sliders_.begin(); it != sliders_.end(); ++it) {
        const QJsonObject s = cal.value(it.key()).toObject();
        it->manual = s.value(QStringLiteral("manual")).toInt();
        it->lit = s.value(QStringLiteral("preset")).toBool(true);
        showSlider(it.key());
    }
    filling_ = false;
    sendAll();
}

QJsonObject VideoOutputTool::values() const {
    QJsonObject cal;
    for (auto it = sliders_.begin(); it != sliders_.end(); ++it)
        cal.insert(it.key(), QJsonObject{{QStringLiteral("manual"), it->manual}, {QStringLiteral("preset"), it->lit}});
    return {{QStringLiteral("syncLock"), sync_->currentText()},
            {QStringLiteral("sdOutput"), sdOutput_->currentText()},
            {QStringLiteral("hdOutput"), hdOutput_->currentText()},
            {QStringLiteral("hdmiColorSpace"), hdmiColor_->currentText()},
            {QStringLiteral("hdmiFormat"), hdmiFormat_->currentText()},
            {QStringLiteral("crossconvert"), crossconvert_->currentText()},
            {QStringLiteral("downconvert"), downconvert_->currentText()},
            {QStringLiteral("componentFormat"), componentFormat_->currentText()},
            {QStringLiteral("vbi"), vbi_->currentText()},
            {QStringLiteral("calibration"), cal}};
}

double VideoOutputTool::sliderValue(const QString& name) const {
    const auto it = sliders_.constFind(name);
    if (it == sliders_.constEnd()) return 0;
    return it->lit ? 0.0 : it->manual;
}

bool VideoOutputTool::presetLit(const QString& name) const { return sliders_.value(name).lit; }

void VideoOutputTool::moveSlider(const QString& name, int value) {
    auto it = sliders_.find(name);
    if (it == sliders_.end()) return;
    // "When you click a slider of a lit preset button, the button dims (appears gray), and the slider returns to the
    // most recent manual level setting"; moving it then sets that level.
    if (it->lit) {
        it->lit = false;
    } else {
        it->manual = std::clamp(value, -100, 100);
    }
    showSlider(name);
    if (session_) session_->setCalibration(name.toStdString(), sliderValue(name));
    changed();
}

void VideoOutputTool::clickPreset(const QString& name) {
    auto it = sliders_.find(name);
    if (it == sliders_.end()) return;
    // "When you click an unlit preset button, it becomes lit (green), and the slider moves to the factory preset level";
    // clicking a lit one shows the manual level again (ours: "switch the preset buttons between the levels you set
    // manually and the factory preset levels").
    it->lit = !it->lit;
    showSlider(name);
    if (session_) session_->setCalibration(name.toStdString(), sliderValue(name));
    changed();
}

void VideoOutputTool::chooseTestPattern(const QString& name) {
    const int i = testPatterns_->findText(name);
    if (i >= 0) testPatterns_->setCurrentIndex(i);
}

void VideoOutputTool::showSlider(const QString& name) {
    Slider& s = sliders_[name];
    const bool was = filling_;
    filling_ = true;
    const int shown = s.lit ? 0 : s.manual;
    s.slider->setValue(shown);
    s.preset->setChecked(s.lit);
    s.value->setText(QString::number(shown));
    filling_ = was;
}

void VideoOutputTool::sendAll() {
    if (!session_) return;
    session_->setSyncSource(sync_->currentText().toStdString());
    session_->setOutputSignal(sdOutput_->currentText().toStdString());
    for (auto it = sliders_.begin(); it != sliders_.end(); ++it) session_->setCalibration(it.key().toStdString(), sliderValue(it.key()));
}

void VideoOutputTool::changed() {
    if (filling_) return;
    if (session_) {
        session_->setSyncSource(sync_->currentText().toStdString());
        // The output being calibrated is the SD Cal tab's or, on the HD Cal tab, the HD one.
        session_->setOutputSignal((tabs_->currentIndex() == 2 ? hdOutput_ : sdOutput_)->currentText().toStdString());
        // "Bars are displayed on the Client monitor" (p.915).
        const QImage pattern = media::testPattern(testPatterns_->currentText(), QSize(1920, 1080));
        if (!pattern.isNull()) {
            const QImage rgba = pattern.convertToFormat(QImage::Format_RGBA8888);
            session_->showFrame(rgba.constBits(), rgba.width(), rgba.height(), static_cast<int>(rgba.bytesPerLine()));
        }
    }
    emit valuesChanged(values());
}

} // namespace lumenis::ui
