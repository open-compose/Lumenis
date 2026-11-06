// The Video Output tool (Phase 7.3b; docs/spec/video-output.md; Editing Guide p.910-917, p.1225-1227): Tools > Video
// Output Tool with its Options, SD Cal and HD Cal tabs for the video output hardware of an ext::VideoOutputProvider.
// Its values are the project's "Video Output" setting ("Output settings are Project settings, available to all users
// and all projects on the system", p.915), which MainWindow keeps; the tool sends them to the open device.
#pragma once

#include "lumenis/ext/video_output.h"

#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QWidget>

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTabWidget;

namespace lumenis::ui {

class VideoOutputTool : public QWidget {
    Q_OBJECT
public:
    // The calibration sliders of the SD Cal and HD Cal tabs (p.914-917), each -100 to 100 around its factory level 0.
    static QStringList sdSliders();
    static QStringList hdSliders();

    explicit VideoOutputTool(QWidget* parent = nullptr);
    ~VideoOutputTool() override;

    // The hardware: the first device of the providers is used. Without any the tool is not available (p.1225).
    void setDevices(std::vector<std::shared_ptr<ext::VideoOutputProvider>> providers);
    bool isAvailable() const { return session_ != nullptr; }
    // The project's Video Output values: syncLock, sdOutput, hdOutput, hdmiColorSpace, hdmiFormat, crossconvert,
    // downconvert, componentFormat, vbi, testPattern and calibration {slider: {manual, preset}}.
    void setValues(const QJsonObject& values);
    QJsonObject values() const;

    // A slider's value as shown: the factory level (0) while its preset button is lit, else its manual level.
    double sliderValue(const QString& name) const;
    void moveSlider(const QString& name, int value);
    void clickPreset(const QString& name);
    bool presetLit(const QString& name) const;
    void chooseTestPattern(const QString& name);

signals:
    // After any change, for the project's setting.
    void valuesChanged(const QJsonObject& values);
    // Settings > Save As with its name (p.915).
    void saveAsRequested(const QString& name);

private:
    struct Slider {
        QSlider* slider = nullptr;
        QPushButton* preset = nullptr;
        QLabel* value = nullptr;
        int manual = 0;
        bool lit = true; // "all preset buttons are lit (green), with the factory presets loaded for each slider"
    };
    QWidget* calibrationTab(const QStringList& sliders, QComboBox* output);
    void showSlider(const QString& name);
    void sendAll();
    void changed();

    std::unique_ptr<ext::VideoOutputSession> session_;
    ext::VideoOutputDeviceInfo device_;
    QStackedWidget* stack_ = nullptr;
    QLabel* unavailable_ = nullptr;
    QLabel* deviceName_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QComboBox* sync_ = nullptr;
    QComboBox* hdmiColor_ = nullptr;
    QComboBox* hdmiFormat_ = nullptr;
    QComboBox* crossconvert_ = nullptr;
    QComboBox* downconvert_ = nullptr;
    QComboBox* componentFormat_ = nullptr;
    QComboBox* vbi_ = nullptr;
    QComboBox* testPatterns_ = nullptr;
    QComboBox* sdOutput_ = nullptr;
    QComboBox* hdOutput_ = nullptr;
    QMap<QString, Slider> sliders_;
    bool filling_ = false;
};

} // namespace lumenis::ui
