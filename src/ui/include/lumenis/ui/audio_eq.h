#pragma once

#include "lumenis/model/audio_eq.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QWidget>

#include <functional>
#include <optional>

class QLabel;
class QLineEdit;
class QMenu;
class QSlider;
class QToolButton;

namespace lumenis::ui {

class Timeline;

// The EQ Parameter graph (7 in the picture on p.737): the EQ's frequency response
// (model::eqResponse), 20 Hz to 20 kHz on a log scale, +15 dB at the top and -20 dB at the bottom,
// with the zero line and lines at 50 Hz, 100 Hz, 500 Hz, 3 kHz and 12 kHz.
class EqGraph : public QWidget {
    Q_OBJECT // its class name reaches the accessibility factory (accessibility.h)

public:
    explicit EqGraph(QWidget* parent = nullptr);
    void setEq(const model::AudioEq& eq);
    const model::AudioEq& eq() const { return eq_; }
    // The graph's x for a frequency and its y for a gain, in widget pixels.
    double xFor(double hz) const;
    double yFor(double db) const;
    QSize sizeHint() const override { return {360, 120}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    model::AudioEq eq_;
};

// Tools > Audio EQ (p.736-745): the three bands of the segment under the Record position on the
// tool's track, and the Fast menu's Set EQ - In/Out and Remove EQs items.
//
// A panel's contents ("AudioEQ"), made by MainWindow, which reads bypassed() for what plays. It
// follows Timeline::changed and Composer::changed. Each finished change of a control sets the EQ
// on the segment under the position on every selected audio track, as an undo step
// (Timeline::setEq); the values shown are those of the tool's track.
class AudioEqTool : public QWidget {
    Q_OBJECT

public:
    enum Band { Low = 0, Mid = 1, High = 2 };

    // `timeline` must outlive the tool.
    explicit AudioEqTool(Timeline& timeline, QWidget* parent = nullptr);

    // The tool's track: the first selected audio track of the Record monitor's sequence (p.739);
    // empty without one.
    QString track() const;
    // The selected audio tracks, in order: the ones the controls set.
    QStringList tracks() const;
    // The values shown: the EQ of the segment under the position on track(), else the last ones
    // set (so an EQ can be made before Set EQ - In/Out copies it).
    const model::AudioEq& eq() const { return eq_; }
    // Whether the segment under the position on track() has an EQ.
    bool hasEq() const { return current_.has_value(); }

    // The controls (pictures on p.736-738).
    QToolButton* trackButton() const { return track_; }
    QToolButton* inButton() const { return in_; }
    QToolButton* bypassButton() const { return bypass_; }
    QLineEdit* gainBox(Band band) const { return boxes_[band]; }
    QSlider* slider(Band band) const { return sliders_[band]; }
    // The low turnover, the midrange's bandwidth, the high turnover: each click takes the next value.
    QToolButton* bandButton(Band band) const { return bandButtons_[band]; }
    QSlider* rangeSlider() const { return range_; }
    QLabel* rangeReadout() const { return readout_; }
    EqGraph* graph() const { return graph_; }
    QLabel* messageLabel() const { return message_; }
    // Fills the Fast menu for the current track and returns it (the tool owns it).
    QMenu* fastMenu();

    // The predefined templates of the Fast menu (p.743-744), in order, from
    // src/ui/resources/eq_templates.json (docs/spec/audio-eq.md says where each one's values come
    // from).
    static const QList<model::EqTemplate>& templates();
    // A template on the segment under the position on every selected audio track, as an undo step;
    // its values go to the Console (p.744-745).
    bool applyTemplate(const model::EqTemplate& t);
    // A template by name and EQ (the Site_EQs_Bin's, p.745), the same way.
    bool applyTemplate(const QString& name, const model::AudioEq& eq);
    // The EQ templates of Site_EQs_Bin (p.745), listed in the Fast menu after the predefined ones;
    // MainWindow reads the bin each time the menu opens.
    using SiteTemplates = std::function<QList<model::BinItem>()>;
    void setSiteTemplates(SiteTemplates source) { siteTemplates_ = std::move(source); }
    // The effect icon (p.736): dragging it to a bin makes an EQ template of the values shown, and to
    // an audio clip in the Timeline puts them there (p.740; eqTemplateMime()).
    QToolButton* effectIcon() const { return effectIcon_; }
    // What the effect icon drags: {"name", "eq"} as eqTemplateMime() describes.
    QByteArray iconTemplate() const;

    // Bypass RT EQ (p.737): the EQ effects are not heard. Kept in the tool; MainWindow applies it.
    bool bypassed() const;
    void setBypassed(bool on);

    // Sets the values shown and, with `done`, on the segments (an undo step). Values are put in
    // range by model::clampEq.
    void setEq(const model::AudioEq& eq, bool done = true);
    // Set EQ - In/Out (p.740): the EQ of the segment under the position on track() on its whole
    // segments between the marks (IN to OUT, IN to the end, the whole track without marks); if
    // that segment has none, the range's EQs are removed.
    bool setEqInOut();
    // Remove EQs On Track - In/Out and On Enabled Tracks - In/Out (p.740).
    bool removeEqs(bool enabledTracks);

    // The EQ Range slider's position (0-kRangeSteps) for a midrange frequency, log scaled from
    // 50 Hz to 15 kHz, and back.
    static constexpr int kRangeSteps = 1000;
    static int rangePosition(double hz);
    static double rangeFrequency(int position);
    // "500 Hz", "2.5 kHz".
    static QString frequencyText(double hz);

    // Shows the values of the segment under the position.
    void refresh();

signals:
    void bypassChanged(bool on);

protected:
    // Typing on a slider (p.739): digits and '-' build a value, Enter sets it.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showEq(const model::AudioEq& eq);
    void fail(const QString& text);

    Timeline& timeline_;
    model::AudioEq eq_;
    std::optional<model::AudioEq> current_;
    QToolButton* track_ = nullptr;
    QToolButton* in_ = nullptr;
    QToolButton* bypass_ = nullptr;
    QToolButton* fast_ = nullptr;
    QToolButton* effectIcon_ = nullptr;
    QPoint pressAt_;
    SiteTemplates siteTemplates_;
    QLineEdit* boxes_[3]{};
    QSlider* sliders_[3]{};
    QToolButton* bandButtons_[3]{};
    QSlider* range_ = nullptr;
    QLabel* readout_ = nullptr;
    EqGraph* graph_ = nullptr;
    QLabel* message_ = nullptr;
    QString typed_;        // a value being typed on a slider
    bool syncing_ = false; // showEq() is setting the controls; their signals are ignored
};

} // namespace lumenis::ui
