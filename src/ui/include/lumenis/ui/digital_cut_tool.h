// The Digital Cut tool (Phase 7.3a; docs/spec/digital-cut.md; Editing Guide p.926-938): File > Output > Digital Cut,
// "controls when you record a sequence to tape". The tool collects the choices and shows the state; MainWindow::
// digitalCut runs the preview or the recording through an ext::DeckProvider deck.
#pragma once

#include "lumenis/ext/deck.h"

#include <QStringList>
#include <QWidget>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace lumenis::ui {

class DigitalCutTool : public QWidget {
    Q_OBJECT
public:
    // Where on the tape the recording starts (p.929-930).
    enum class Start { SequenceTime, RecordDeckTime, MarkInTime, IgnoreTime };

    struct Options {
        bool entireSequence = true;   // "ignore any IN or OUT points"
        bool safeMode = true;         // Digital Cut Safe Mode, "selected by default"
        bool stopOnDroppedFrames = false;
        bool addBlack = false;
        QString blackDuration;        // Add Black at Tail's timecode
        bool ltcPreroll = false;      // LTC out during preroll (p.911-912)
        bool remote = true;           // Remote or Local deck control
        ext::DeckRecordMode mode = ext::DeckRecordMode::Insert;
        Start start = Start::SequenceTime;
        QString markIn;               // Mark In Time
        bool customPreroll = false;
        int prerollSeconds = 5;
        bool customOffset = false;
        int frameOffset = 0;          // -5 to 5 frames (p.930)
        QString outputMode;
        int bitDepth = 8;
        QStringList sequenceTracks;   // the Sequence Track buttons that are on
        bool tapeVideo = true;        // the Enable Track buttons (Remote only)
        QList<int> tapeAudio{1, 2};
    };

    explicit DigitalCutTool(QWidget* parent = nullptr);

    // The decks of every deck provider (the Deck Selection menu, p.927); "No Deck" without any.
    void setDecks(std::vector<std::shared_ptr<ext::DeckProvider>> providers);
    // The chosen deck's provider and id; null without one.
    std::shared_ptr<ext::DeckProvider> deckProvider() const;
    std::string deckId() const;
    bool chooseDeck(const QString& name);
    // The Record monitor's sequence's tracks for the Sequence Track buttons, all on; empty without a sequence ("You
    // cannot access digital cut options without a sequence loaded", p.928).
    void setSequenceTracks(const QStringList& tracks);

    Options options() const;
    void setOptions(const Options& options);
    // The state line under the buttons: idle, previewing, recording, the last result.
    void setStatus(const QString& text);
    // While previewing or recording only Halt is live.
    void setRunning(bool running);

signals:
    void previewRequested();
    void playRequested();
    void haltRequested();
    // Check Decks (p.927): MainWindow lists the decks again.
    void checkDecksRequested();

private:
    void fillStartMenu();
    void updateEnabled();

    struct DeckEntry {
        std::shared_ptr<ext::DeckProvider> provider;
        ext::DeckInfo info;
    };
    std::vector<DeckEntry> decks_;
    std::vector<std::shared_ptr<ext::DeckProvider>> providers_;
    QComboBox* outputMode_ = nullptr;
    QComboBox* bitDepth_ = nullptr;
    QCheckBox* entire_ = nullptr;
    QCheckBox* safe_ = nullptr;
    QCheckBox* stopOnDropped_ = nullptr;
    QCheckBox* addBlack_ = nullptr;
    QLineEdit* blackDuration_ = nullptr;
    QCheckBox* ltcPreroll_ = nullptr;
    QRadioButton* remote_ = nullptr;
    QRadioButton* local_ = nullptr;
    QComboBox* deckMenu_ = nullptr;
    QComboBox* startMenu_ = nullptr;
    QComboBox* editMenu_ = nullptr;
    QLineEdit* markIn_ = nullptr;
    QCheckBox* customPreroll_ = nullptr;
    QSpinBox* preroll_ = nullptr;
    QCheckBox* customOffset_ = nullptr;
    QSpinBox* offset_ = nullptr;
    QHBoxLayout* sequenceRow_ = nullptr;
    QList<QPushButton*> sequenceButtons_;
    QList<QPushButton*> tapeButtons_;
    QPushButton* preview_ = nullptr;
    QPushButton* play_ = nullptr;
    QPushButton* halt_ = nullptr;
    QLabel* status_ = nullptr;
    bool running_ = false;
};

} // namespace lumenis::ui
