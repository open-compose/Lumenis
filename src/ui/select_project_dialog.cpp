#include "lumenis/ui/select_project_dialog.h"

#include "lumenis/core/branding.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lumenis::ui {

namespace {

// Columns of the project list (guide p.61-62).
enum Column { Name, Frame, ProjectType, FrameRate, ColorSpace, Raster, Stereo, Created, Modified, ColumnCount };

const char* const kColumnTitles[ColumnCount] = {
    "Name", "Frame", "Project Type", "Frame Rate", "Color Space", "Raster Dimension", "Stereoscopic",
    "Creation Date", "Modified Date"};

const char* const kFilmTypes[] = {"16mm", "35mm, 3 perf", "35mm, 4 perf"}; // guide p.107

QString dateText(const QDateTime& t) {
    // A fixed format: locale-dependent dates would make screenshots and
    // column widths differ between machines.
    return t.isValid() ? t.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
}

QString str(const std::string& s) {
    return QString::fromStdString(s);
}

QString aspectText(const Rational& a) {
    if (a == Rational(16, 9) || a == Rational(4, 3)) {
        return QStringLiteral("%1:%2").arg(a.num()).arg(a.den());
    }
    return QStringLiteral("%1:1").arg(a.toDouble(), 0, 'f', 2);
}

// Sorts by name without regard to case, like the project store and file
// browsers ("apple" before "Zebra").
class ProjectItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem& other) const override {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        return text(column).compare(other.text(column), Qt::CaseInsensitive) < 0;
    }
};

// A neutral placeholder until projects carry a poster frame (set from the
// Composer in a later phase).
QPixmap placeholderFrame(const QSize& size) {
    QPixmap pm(size);
    pm.fill(QColor(0x22, 0x22, 0x22));
    QPainter p(&pm);
    p.setPen(QColor(0x55, 0x55, 0x55));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return pm;
}

// The data stored with each Format menu entry: group, width, height, scan.
struct FormatKey {
    std::string group;
    int width = 0;
    int height = 0;
    ScanMode scan = ScanMode::Progressive;
};

QVariant toVariant(const FormatKey& k) {
    return QVariantList{str(k.group), k.width, k.height, k.scan == ScanMode::Interlaced};
}

std::optional<FormatKey> fromVariant(const QVariant& v) {
    const QVariantList l = v.toList();
    if (l.size() != 4) {
        return std::nullopt; // "Custom"
    }
    return FormatKey{l[0].toString().toStdString(), l[1].toInt(), l[2].toInt(),
                     l[3].toBool() ? ScanMode::Interlaced : ScanMode::Progressive};
}

QString formatKeyLabel(const FormatKey& k, FormatFamily family) {
    const QString raster = QStringLiteral("%1 x %2").arg(k.width).arg(k.height);
    if (family == FormatFamily::HD) {
        return QStringLiteral("%1%2  (%3)").arg(k.height).arg(k.scan == ScanMode::Interlaced ? 'i' : 'p').arg(raster);
    }
    if (family == FormatFamily::SD) {
        return QStringLiteral("%1 %2  (%3)")
            .arg(str(k.group), k.scan == ScanMode::Interlaced ? QStringLiteral("interlaced") : QStringLiteral("progressive"),
                 raster);
    }
    return QStringLiteral("%1  (%2)").arg(str(k.group), raster);
}

} // namespace

SelectProjectDialog::SelectProjectDialog(const model::Locations& locations, QWidget* parent)
    : QDialog(parent), locations_(locations) {
    setObjectName(QStringLiteral("SelectProjectDialog"));
    setWindowTitle(tr("Select Project"));
    resize(980, 640);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(24, 16, 24, 16);

    auto* welcome = new QLabel(tr("Welcome to %1").arg(QString::fromUtf8(branding::kProductName)), right);
    userTitle_ = new QLabel(right);
    userTitle_->setObjectName(QStringLiteral("userTitle"));
    QFont big = userTitle_->font();
    big.setPixelSize(22);
    big.setWeight(QFont::DemiBold);
    userTitle_->setFont(big);
    rightLayout->addWidget(welcome);
    rightLayout->addWidget(userTitle_);

    // Projects | New Project
    auto* tabRow = new QHBoxLayout;
    tabs_ = new QButtonGroup(this);
    tabs_->setExclusive(true);
    for (const auto& [text, id] : {std::pair{tr("Projects"), 0}, std::pair{tr("New Project"), 1}}) {
        auto* b = new QPushButton(text, right);
        b->setObjectName(id == 0 ? QStringLiteral("projectsTab") : QStringLiteral("newProjectTab"));
        b->setCheckable(true);
        b->setProperty("toggle", true);
        b->setMinimumWidth(110);
        b->setFixedHeight(kControlHeight);
        tabs_->addButton(b, id);
    }
    tabRow->addStretch();
    tabRow->addWidget(tabs_->button(0));
    tabRow->addWidget(tabs_->button(1));
    tabRow->addStretch();
    rightLayout->addLayout(tabRow);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    users_ = new QComboBox(right);
    users_->setObjectName(QStringLiteral("userProfile"));
    form->addRow(tr("User Profile:"), users_);
    auto* locationRow = new QHBoxLayout;
    location_ = new QComboBox(right);
    location_->setObjectName(QStringLiteral("location"));
    location_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* browse = new QToolButton(right);
    browse->setObjectName(QStringLiteral("browse"));
    browse->setText(QStringLiteral("…"));
    browse->setToolTip(tr("Browse for a project folder"));
    locationRow->addWidget(location_);
    locationRow->addWidget(browse);
    form->addRow(tr("Location:"), locationRow);
    rightLayout->addLayout(form);

    pages_ = new QStackedWidget(right);
    pages_->addWidget(buildProjectsPage());
    pages_->addWidget(buildNewProjectPage());
    pages_->addWidget(buildWhatsNewPage());
    rightLayout->addWidget(pages_, 1);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildSidebar());
    root->addWidget(right, 1);

    connect(tabs_, &QButtonGroup::idClicked, this, [this](int id) {
        showPage(id == 0 ? Page::Projects : Page::NewProject);
    });
    connect(users_, &QComboBox::currentTextChanged, userTitle_, &QLabel::setText);
    connect(location_, &QComboBox::currentIndexChanged, this, &SelectProjectDialog::refresh);
    connect(browse, &QToolButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Project Directory"), currentFolder());
        if (!dir.isEmpty()) {
            setExternalFolder(dir);
        }
    });

    // Make the selected tab and sidebar entry obvious; Fusion's checked
    // push button is too subtle on a dark palette.
    setStyleSheet(QStringLiteral(
        "QPushButton[toggle=\"true\"] { border: 1px solid palette(dark); border-radius: 3px; padding: 4px 14px;"
        "  background-color: palette(button); }"
        "QPushButton[toggle=\"true\"]:hover { background-color: palette(midlight); }"
        "QPushButton[toggle=\"true\"]:checked { background-color: palette(highlight); color: palette(highlighted-text); }"));

    populateUsers();
    populateLocations();
    populateFormats();
    showPage(Page::Projects);
    refresh();
}

QWidget* SelectProjectDialog::buildSidebar() {
    auto* side = new QWidget(this);
    side->setObjectName(QStringLiteral("sidebar"));
    side->setFixedWidth(170);
    side->setAutoFillBackground(true);
    QPalette pal = side->palette();
    pal.setColor(QPalette::Window, pal.color(QPalette::Window).darker(115));
    side->setPalette(pal);

    auto* layout = new QVBoxLayout(side);
    layout->setContentsMargins(12, 56, 12, 12);
    sidebar_ = new QButtonGroup(this);
    for (const auto& [text, id] : {std::pair{tr("Select Project"), 0}, std::pair{tr("What's New"), 1}}) {
        auto* b = new QPushButton(text, side);
        b->setObjectName(id == 0 ? QStringLiteral("selectProjectButton") : QStringLiteral("whatsNewButton"));
        b->setCheckable(true);
        b->setProperty("toggle", true);
        b->setFixedHeight(kControlHeight);
        sidebar_->addButton(b, id);
        layout->addWidget(b);
    }
    layout->addStretch();
    connect(sidebar_, &QButtonGroup::idClicked, this, [this](int id) {
        showPage(id == 0 ? Page::Projects : Page::WhatsNew);
    });
    return side;
}

QWidget* SelectProjectDialog::buildProjectsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);

    // View controls (guide p.62): size slider, text view, frame view, fast menu.
    auto* tools = new QHBoxLayout;
    tools->addStretch();
    frameSize_ = new QSlider(Qt::Horizontal, page);
    frameSize_->setObjectName(QStringLiteral("frameSize"));
    frameSize_->setRange(48, 192);
    frameSize_->setValue(96);
    frameSize_->setFixedWidth(100);
    auto* textView = new QToolButton(page);
    textView->setObjectName(QStringLiteral("textView"));
    textView->setIcon(glyphIcon(GlyphIcon::ListView));
    textView->setToolTip(tr("Text view"));
    textView->setCheckable(true);
    textView->setChecked(true);
    auto* frameView = new QToolButton(page);
    frameView->setObjectName(QStringLiteral("frameView"));
    frameView->setIcon(glyphIcon(GlyphIcon::FrameView));
    frameView->setToolTip(tr("Frame view"));
    frameView->setCheckable(true);
    auto* viewGroup = new QButtonGroup(page);
    viewGroup->addButton(textView, 0);
    viewGroup->addButton(frameView, 1);
    auto* fast = new QToolButton(page);
    fast->setObjectName(QStringLiteral("projectsFastMenu"));
    fast->setIcon(glyphIcon(GlyphIcon::FastMenu));
    fast->setToolTip(tr("Fast menu"));
    fast->setPopupMode(QToolButton::InstantPopup);
    for (QToolButton* b : {textView, frameView, fast}) {
        b->setFixedSize(26, kToolButtonHeight);
        b->setIconSize(QSize(16, 16));
    }
    fast->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    frameSize_->setFixedHeight(kToolButtonHeight);
    tools->addWidget(frameSize_);
    tools->addWidget(textView);
    tools->addWidget(frameView);
    tools->addWidget(fast);
    layout->addLayout(tools);

    table_ = new QTreeWidget(page);
    table_->setObjectName(QStringLiteral("projectTable"));
    table_->setRootIsDecorated(false);
    table_->setUniformRowHeights(true);
    table_->setAlternatingRowColors(true);
    table_->setSortingEnabled(true);
    table_->setColumnCount(ColumnCount);
    QStringList headers;
    for (const char* title : kColumnTitles) {
        headers << tr(title);
    }
    table_->setHeaderLabels(headers);
    table_->header()->setStretchLastSection(false);
    table_->header()->setSectionResizeMode(QHeaderView::Interactive);
    table_->setColumnWidth(Name, 170);
    table_->setColumnWidth(Frame, 50);
    table_->setColumnWidth(ProjectType, 120);
    table_->setColumnWidth(FrameRate, 80);
    table_->setColumnWidth(ColorSpace, 90);
    table_->setColumnWidth(Raster, 120);
    table_->setColumnWidth(Stereo, 95);
    table_->setColumnWidth(Created, 125);
    table_->setColumnWidth(Modified, 125);
    table_->header()->setFixedHeight(kHeaderHeight);
    table_->installEventFilter(this);

    frames_ = new QListWidget(page);
    frames_->setObjectName(QStringLiteral("projectFrames"));
    frames_->setViewMode(QListView::IconMode);
    frames_->setResizeMode(QListView::Adjust);
    frames_->setMovement(QListView::Static);
    frames_->setSpacing(8);
    frames_->installEventFilter(this);

    projectViews_ = new QStackedWidget(page);
    projectViews_->addWidget(table_);
    projectViews_->addWidget(frames_);
    layout->addWidget(projectViews_, 1);

    // Fast menu: choose which columns show (guide p.62).
    auto* menu = new QMenu(fast);
    QMenu* columns = menu->addMenu(tr("Columns"));
    for (int c = Frame; c < ColumnCount; ++c) {
        QAction* a = columns->addAction(tr(kColumnTitles[c]));
        a->setCheckable(true);
        a->setChecked(true);
        connect(a, &QAction::toggled, this, [this, c](bool on) { table_->setColumnHidden(c, !on); });
    }
    fast->setMenu(menu);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* quitButton = new QPushButton(tr("Quit"), page);
    quitButton->setObjectName(QStringLiteral("quitButton"));
    open_ = new QPushButton(tr("Open"), page);
    open_->setObjectName(QStringLiteral("openButton"));
    open_->setDefault(true);
    buttons->addWidget(quitButton);
    buttons->addWidget(open_);
    layout->addLayout(buttons);

    connect(viewGroup, &QButtonGroup::idClicked, projectViews_, &QStackedWidget::setCurrentIndex);
    connect(frameSize_, &QSlider::valueChanged, this, [this](int v) {
        frames_->setIconSize(QSize(v, v * 9 / 16));
    });
    frames_->setIconSize(QSize(frameSize_->value(), frameSize_->value() * 9 / 16));
    connect(quitButton, &QPushButton::clicked, this, &SelectProjectDialog::quit);
    connect(open_, &QPushButton::clicked, this, &SelectProjectDialog::openSelected);
    connect(table_, &QTreeWidget::itemDoubleClicked, this, &SelectProjectDialog::openSelected);
    connect(frames_, &QListWidget::itemDoubleClicked, this, &SelectProjectDialog::openSelected);
    // Keep both views' selections in step.
    connect(table_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto items = table_->selectedItems();
        if (!items.isEmpty()) {
            const auto found = frames_->findItems(items.first()->text(Name), Qt::MatchExactly);
            if (!found.isEmpty()) frames_->setCurrentItem(found.first());
        }
        open_->setEnabled(!items.isEmpty());
    });
    connect(frames_, &QListWidget::itemSelectionChanged, this, [this] {
        const auto items = frames_->selectedItems();
        if (!items.isEmpty()) selectProject(items.first()->text());
    });
    return page;
}

QWidget* SelectProjectDialog::buildNewProjectPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 16, 0, 0);

    auto* top = new QFormLayout;
    top->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    name_ = new QLineEdit(tr("Untitled Project"), page);
    name_->setObjectName(QStringLiteral("projectName"));
    name_->setMaxLength(kMaxProjectNameLength);
    top->addRow(tr("Project name:"), name_);

    auto* settingsRow = new QHBoxLayout;
    settingsToggle_ = new QToolButton(page);
    settingsToggle_->setObjectName(QStringLiteral("settingsToggle"));
    settingsToggle_->setText(tr("Settings:"));
    settingsToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    settingsToggle_->setArrowType(Qt::RightArrow);
    settingsToggle_->setCheckable(true);
    settingsToggle_->setAutoRaise(true);
    chooseForMe_ = new QCheckBox(tr("Choose for me"), page);
    chooseForMe_->setObjectName(QStringLiteral("chooseForMe"));
    settingsRow->addWidget(settingsToggle_);
    settingsRow->addWidget(chooseForMe_);
    settingsRow->addStretch();
    layout->addLayout(top);
    layout->addLayout(settingsRow);

    settingsPanel_ = new QWidget(page);
    auto* f = new QFormLayout(settingsPanel_);
    f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    format_ = new QComboBox(settingsPanel_);
    format_->setObjectName(QStringLiteral("format"));
    f->addRow(tr("Format:"), format_);
    auto* rasterRow = new QHBoxLayout;
    rasterWidth_ = new QSpinBox(settingsPanel_);
    rasterWidth_->setObjectName(QStringLiteral("rasterWidth"));
    rasterHeight_ = new QSpinBox(settingsPanel_);
    rasterHeight_->setObjectName(QStringLiteral("rasterHeight"));
    for (QSpinBox* s : {rasterWidth_, rasterHeight_}) {
        s->setRange(2, 16384);
        s->setSingleStep(2);
        s->setFixedWidth(80); // not from digit widths, which vary by font engine
    }
    rasterRow->addWidget(rasterWidth_);
    rasterRow->addWidget(new QLabel(QStringLiteral("x"), settingsPanel_));
    rasterRow->addWidget(rasterHeight_);
    rasterRow->addStretch();
    f->addRow(tr("Raster Dimension:"), rasterRow);
    aspect_ = new QComboBox(settingsPanel_);
    aspect_->setObjectName(QStringLiteral("aspectRatio"));
    f->addRow(tr("Aspect Ratio:"), aspect_);
    frameRate_ = new QComboBox(settingsPanel_);
    frameRate_->setObjectName(QStringLiteral("frameRate"));
    f->addRow(tr("Frame Rate:"), frameRate_);
    colorSpace_ = new QComboBox(settingsPanel_);
    colorSpace_->setObjectName(QStringLiteral("colorSpace"));
    f->addRow(tr("Color Space:"), colorSpace_);
    colorDepth_ = new QComboBox(settingsPanel_);
    colorDepth_->setObjectName(QStringLiteral("colorDepth"));
    for (const std::string& d : colorDepths()) colorDepth_->addItem(str(d));
    colorDepth_->setCurrentText(QStringLiteral("10 bit"));
    f->addRow(tr("Color Depth:"), colorDepth_);
    stereoscopic_ = new QComboBox(settingsPanel_);
    stereoscopic_->setObjectName(QStringLiteral("stereoscopic"));
    for (const std::string& m : stereoscopicModes()) stereoscopic_->addItem(str(m));
    f->addRow(tr("Stereoscopic:"), stereoscopic_);
    auto* filmRow = new QHBoxLayout;
    film_ = new QCheckBox(tr("Film"), settingsPanel_);
    film_->setObjectName(QStringLiteral("film"));
    filmType_ = new QComboBox(settingsPanel_);
    filmType_->setObjectName(QStringLiteral("filmType"));
    for (const char* t : kFilmTypes) filmType_->addItem(QString::fromLatin1(t));
    filmType_->setCurrentText(QStringLiteral("35mm, 4 perf"));
    film_->setFixedWidth(70); // keeps the film type menu in place whatever the text width
    filmRow->addWidget(film_);
    filmRow->addWidget(filmType_);
    filmRow->addStretch();
    f->addRow(tr("Film:"), filmRow);
    layout->addWidget(settingsPanel_);
    settingsPanel_->setVisible(false);

    formError_ = new QLabel(page);
    formError_->setObjectName(QStringLiteral("formError"));
    QPalette errorPalette = formError_->palette();
    errorPalette.setColor(QPalette::WindowText, QColor(0xff, 0x8a, 0x80));
    formError_->setPalette(errorPalette);
    layout->addWidget(formError_);
    layout->addStretch();

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* ok = new QPushButton(tr("OK"), page);
    ok->setObjectName(QStringLiteral("createButton"));
    buttons->addWidget(ok);
    layout->addLayout(buttons);

    connect(settingsToggle_, &QToolButton::toggled, this, [this](bool open) {
        settingsToggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        settingsPanel_->setVisible(open);
    });
    connect(chooseForMe_, &QCheckBox::toggled, this, &SelectProjectDialog::updateFormatDependentFields);
    connect(format_, &QComboBox::currentIndexChanged, this, &SelectProjectDialog::updateFormatDependentFields);
    connect(rasterWidth_, &QSpinBox::valueChanged, this, &SelectProjectDialog::updateFormatDependentFields);
    connect(rasterHeight_, &QSpinBox::valueChanged, this, &SelectProjectDialog::updateFormatDependentFields);
    connect(frameRate_, &QComboBox::currentIndexChanged, this, [this] {
        const auto chosen = chosenFormat();
        const bool filmOk = chosen && filmAvailable(*chosen) && !chooseForMe_->isChecked();
        film_->setEnabled(filmOk);
        filmType_->setEnabled(filmOk && film_->isChecked());
    });
    connect(film_, &QCheckBox::toggled, filmType_, &QWidget::setEnabled);
    connect(name_, &QLineEdit::returnPressed, this, &SelectProjectDialog::createProjectFromForm);
    connect(ok, &QPushButton::clicked, this, &SelectProjectDialog::createProjectFromForm);
    return page;
}

QWidget* SelectProjectDialog::buildWhatsNewPage() {
    auto* browser = new QTextBrowser(this);
    browser->setObjectName(QStringLiteral("whatsNew"));
    browser->setOpenExternalLinks(true);
    QFile notes(QStringLiteral(":/lumenis/ui/whats_new.html"));
    QString html = notes.open(QIODevice::ReadOnly) ? QString::fromUtf8(notes.readAll()) : QString();
    html.replace(QStringLiteral("%PRODUCT%"), QString::fromUtf8(branding::kProductName));
    html.replace(QStringLiteral("%VERSION%"), QString::fromUtf8(branding::kVersion));
    browser->setHtml(html);
    return browser;
}

void SelectProjectDialog::populateUsers() {
    model::UserProfiles profiles(locations_.users);
    QStringList names = profiles.list();
    const QString me = model::UserProfiles::defaultName();
    if (names.isEmpty()) {
        // A first run gets a profile named after the login (guide p.68).
        profiles.create(me);
        names = profiles.list();
        if (names.isEmpty()) {
            names << me; // read-only Users folder: still usable this session
        }
    }
    users_->addItems(names);
    users_->setCurrentText(names.contains(me) ? me : names.first());
    userTitle_->setText(users_->currentText());
}

void SelectProjectDialog::populateLocations() {
    QSignalBlocker block(location_);
    const QVariant current = location_->currentData();
    location_->clear();
    const auto add = [this](const QString& kind, const QString& folder, Location which) {
        const QString shown = locationLabels_.value(static_cast<int>(which), QDir::toNativeSeparators(folder));
        location_->addItem(QStringLiteral("%1: %2").arg(kind, shown), static_cast<int>(which));
    };
    add(tr("Private"), locations_.privateProjects, Location::Private);
    add(tr("Shared"), locations_.sharedProjects, Location::Shared);
    if (!externalFolder_.isEmpty()) {
        add(tr("External"), externalFolder_, Location::External);
    }
    const int keep = location_->findData(current);
    location_->setCurrentIndex(keep >= 0 ? keep : 0);
}

void SelectProjectDialog::setLocationLabel(Location location, const QString& text) {
    if (text.isEmpty()) {
        locationLabels_.remove(static_cast<int>(location));
    } else {
        locationLabels_.insert(static_cast<int>(location), text);
    }
    populateLocations();
}

void SelectProjectDialog::populateFormats() {
    QSignalBlocker block(format_);
    format_->clear();
    std::optional<FormatFamily> lastFamily;
    std::vector<std::string> seen;
    for (const ProjectFormat& f : projectFormatPresets()) {
        const FormatKey key{f.group, f.width, f.height, f.scan};
        const std::string id = f.group + std::to_string(f.width) + "x" + std::to_string(f.height)
            + (f.scan == ScanMode::Interlaced ? "i" : "p");
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        seen.push_back(id);
        if (lastFamily && *lastFamily != f.family) format_->insertSeparator(format_->count());
        lastFamily = f.family;
        format_->addItem(formatKeyLabel(key, f.family), toVariant(key));
    }
    format_->insertSeparator(format_->count());
    format_->addItem(tr("Custom"), QVariant());
    // Default: HD 1080p, the most common delivery format.
    format_->setCurrentIndex(format_->findText(QStringLiteral("1080p  (1920 x 1080)")));
    updateFormatDependentFields();
}

std::optional<ProjectFormat> SelectProjectDialog::chosenFormat() const {
    const auto key = fromVariant(format_->currentData());
    const QVariant rateData = frameRate_->currentData();
    if (!rateData.isValid()) {
        return std::nullopt;
    }
    const auto rate = parseFrameRate(rateData.toString().toStdString());
    if (!rate) {
        return std::nullopt;
    }
    if (key) {
        for (const ProjectFormat& f : projectFormatPresets()) {
            if (f.group == key->group && f.width == key->width && f.height == key->height && f.scan == key->scan
                && f.editRate == *rate) {
                return f;
            }
        }
        return std::nullopt;
    }
    ProjectFormat custom;
    custom.group = "Custom";
    custom.width = rasterWidth_->value();
    custom.height = rasterHeight_->value();
    custom.family = (custom.width > 1920 || custom.height > 1080) ? FormatFamily::HighRes : FormatFamily::HD;
    custom.editRate = *rate;
    custom.scan = ScanMode::Progressive;
    return custom;
}

void SelectProjectDialog::updateFormatDependentFields() {
    const bool manual = !chooseForMe_->isChecked();
    const auto key = fromVariant(format_->currentData());
    const bool custom = !key.has_value();

    // Raster: fixed by a preset, typed for Custom.
    {
        QSignalBlocker bw(rasterWidth_);
        QSignalBlocker bh(rasterHeight_);
        if (key) {
            rasterWidth_->setValue(key->width);
            rasterHeight_->setValue(key->height);
        }
    }
    format_->setEnabled(manual);
    rasterWidth_->setEnabled(manual && custom);
    rasterHeight_->setEnabled(manual && custom);

    // Frame rates the chosen format offers (custom: all progressive rates
    // from 23.98 to 60, guide p.63).
    const QString previousRate = frameRate_->currentData().toString();
    {
        QSignalBlocker b(frameRate_);
        frameRate_->clear();
        if (key) {
            for (const ProjectFormat& f : projectFormatPresets()) {
                if (f.group == key->group && f.width == key->width && f.height == key->height && f.scan == key->scan) {
                    const QString text = str(frameRateText(f.editRate));
                    frameRate_->addItem(text, text);
                }
            }
        } else {
            for (const char* r : {"23.976", "24", "25", "29.97", "30", "47.952", "48", "50", "59.94", "60"}) {
                frameRate_->addItem(QString::fromLatin1(r), QString::fromLatin1(r));
            }
        }
        const int keep = frameRate_->findData(previousRate);
        frameRate_->setCurrentIndex(keep >= 0 ? keep : 0);
    }
    frameRate_->setEnabled(manual);

    const auto format = chosenFormat();
    const FormatFamily family = format ? format->family : FormatFamily::HD;

    // Aspect ratio: SD chooses 4:3 or 16:9, HD is 16:9, high-res follows
    // the raster (guide p.65).
    {
        QSignalBlocker b(aspect_);
        const QString previous = aspect_->currentText();
        aspect_->clear();
        if (family == FormatFamily::SD) {
            aspect_->addItem(QStringLiteral("4:3"), QVariantList{4, 3});
            aspect_->addItem(QStringLiteral("16:9"), QVariantList{16, 9});
            if (aspect_->findText(previous) >= 0) aspect_->setCurrentText(previous);
        } else if (format) {
            const Rational a = format->defaultAspect();
            aspect_->addItem(aspectText(a), QVariantList{static_cast<qlonglong>(a.num()), static_cast<qlonglong>(a.den())});
        }
        aspect_->setEnabled(manual && family == FormatFamily::SD);
    }

    // Colour spaces depend on the family (guide p.65-66).
    {
        QSignalBlocker b(colorSpace_);
        const QString previous = colorSpace_->currentText();
        colorSpace_->clear();
        for (const std::string& c : colorSpacesFor(family)) colorSpace_->addItem(str(c));
        if (colorSpace_->findText(previous) >= 0) colorSpace_->setCurrentText(previous);
    }

    // Stereoscopic is not available in SD (p.66); film only for some
    // progressive HD formats (p.67).
    stereoscopic_->setEnabled(family != FormatFamily::SD);
    if (family == FormatFamily::SD) stereoscopic_->setCurrentIndex(0);
    const bool filmOk = manual && format && filmAvailable(*format);
    film_->setEnabled(filmOk);
    if (!filmOk) film_->setChecked(false);
    filmType_->setEnabled(filmOk && film_->isChecked());
}

model::ProjectSettings SelectProjectDialog::settingsFromForm(QString* error) const {
    model::ProjectSettings s;
    s.name = name_->text();
    s.chooseForMe = chooseForMe_->isChecked();
    const auto format = chosenFormat();
    if (!format) {
        *error = tr("Choose a format and frame rate.");
        return s;
    }
    s.format = *format;
    if (format->group == "Custom") {
        const bool stereo = stereoscopic_->currentIndex() > 0;
        if (const auto problem = validateCustomRaster(format->width, format->height, stereo)) {
            *error = str(*problem);
            return s;
        }
    }
    const QVariantList a = aspect_->currentData().toList();
    s.aspect = a.size() == 2 ? Rational(a[0].toLongLong(), a[1].toLongLong()) : format->defaultAspect();
    s.colorSpace = colorSpace_->currentText();
    s.colorDepth = colorDepth_->currentText();
    s.stereoscopic = stereoscopic_->currentText();
    s.filmType = film_->isChecked() ? filmType_->currentText() : QString();
    return s;
}

bool SelectProjectDialog::createProjectFromForm() {
    QString error;
    const model::ProjectSettings settings = settingsFromForm(&error);
    if (error.isEmpty()) {
        const model::ProjectStore store(currentFolder());
        if (store.create(settings, &error)) {
            formError_->clear();
            name_->setText(tr("Untitled Project"));
            showPage(Page::Projects);
            refresh();
            selectProject(settings.name);
            return true;
        }
    }
    formError_->setText(error);
    return false;
}

bool SelectProjectDialog::deleteSelectedProject(bool askFirst) {
    const auto items = table_->selectedItems();
    if (items.isEmpty()) {
        return false;
    }
    const QString name = items.first()->text(Name);
    if (askFirst) {
        const auto answer = QMessageBox::question(
            this, tr("Delete Project"),
            tr("Delete the project \"%1\" and all of its bins?\nMedia files are not deleted.").arg(name),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Ok) {
            return false;
        }
    }
    QString error;
    if (!model::ProjectStore(currentFolder()).remove(name, &error)) {
        QMessageBox::warning(this, tr("Delete Project"), error);
        return false;
    }
    refresh();
    return true;
}

void SelectProjectDialog::refresh() {
    const QString previous = table_->selectedItems().isEmpty() ? QString() : table_->selectedItems().first()->text(Name);
    entries_ = model::ProjectStore(currentFolder()).list();

    table_->setSortingEnabled(false);
    table_->clear();
    frames_->clear();
    const QPixmap poster = placeholderFrame(QSize(40, 22));
    const QPixmap bigPoster = placeholderFrame(QSize(192, 108));
    for (const model::ProjectEntry& e : entries_) {
        const model::ProjectSettings& s = e.settings;
        auto* item = new ProjectItem(table_);
        item->setText(Name, s.name);
        item->setIcon(Frame, QIcon(poster));
        item->setText(ProjectType, s.chooseForMe ? tr("Choose for Me") : str(s.format.label()));
        item->setText(FrameRate, s.chooseForMe ? QString() : str(frameRateText(s.format.editRate)));
        item->setText(ColorSpace, s.colorSpace);
        item->setText(Raster, s.chooseForMe ? QString()
                                            : QStringLiteral("%1x%2").arg(s.format.width).arg(s.format.height));
        item->setText(Stereo, s.stereoscopic);
        item->setText(Created, dateText(s.created));
        item->setText(Modified, dateText(s.modified));
        item->setData(Name, Qt::UserRole, e.file);
        auto* frame = new QListWidgetItem(QIcon(bigPoster), s.name, frames_);
        frame->setData(Qt::UserRole, e.file);
    }
    table_->setSortingEnabled(true);
    table_->sortByColumn(Name, Qt::AscendingOrder);
    if (!previous.isEmpty()) {
        selectProject(previous);
    }
    open_->setEnabled(!table_->selectedItems().isEmpty());
}

void SelectProjectDialog::openSelected() {
    const auto items = table_->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    selectedFile_ = items.first()->data(Name, Qt::UserRole).toString();
    accept();
}

void SelectProjectDialog::quit() {
    if (confirmQuit_) {
        QMessageBox box(QMessageBox::Question, tr("Quit"),
                        tr("Leave %1?").arg(QString::fromUtf8(branding::kProductName)), QMessageBox::NoButton, this);
        QPushButton* leave = box.addButton(tr("Leave"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != leave) {
            return;
        }
    }
    reject();
}

bool SelectProjectDialog::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == table_ || watched == frames_) && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
            deleteSelectedProject(true);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

QString SelectProjectDialog::userProfile() const {
    return users_->currentText();
}

void SelectProjectDialog::setLocation(Location location) {
    const int index = location_->findData(static_cast<int>(location));
    if (index >= 0) {
        location_->setCurrentIndex(index);
    }
}

void SelectProjectDialog::setExternalFolder(const QString& folder) {
    externalFolder_ = QDir::cleanPath(folder);
    populateLocations();
    setLocation(Location::External);
    refresh();
}

SelectProjectDialog::Location SelectProjectDialog::location() const {
    return static_cast<Location>(location_->currentData().toInt());
}

QString SelectProjectDialog::currentFolder() const {
    switch (location()) {
    case Location::Private: return locations_.privateProjects;
    case Location::Shared: return locations_.sharedProjects;
    case Location::External: return externalFolder_;
    }
    return locations_.privateProjects;
}

void SelectProjectDialog::showPage(Page page) {
    switch (page) {
    case Page::Projects:
        pages_->setCurrentIndex(0);
        tabs_->button(0)->setChecked(true);
        sidebar_->button(0)->setChecked(true);
        break;
    case Page::NewProject:
        pages_->setCurrentIndex(1);
        tabs_->button(1)->setChecked(true);
        sidebar_->button(0)->setChecked(true);
        name_->setFocus();
        name_->selectAll();
        break;
    case Page::WhatsNew:
        pages_->setCurrentIndex(2);
        sidebar_->button(1)->setChecked(true);
        break;
    }
    for (int id : {0, 1}) {
        tabs_->button(id)->setEnabled(page != Page::WhatsNew);
    }
}

SelectProjectDialog::Page SelectProjectDialog::page() const {
    switch (pages_->currentIndex()) {
    case 1: return Page::NewProject;
    case 2: return Page::WhatsNew;
    default: return Page::Projects;
    }
}

bool SelectProjectDialog::selectProject(const QString& name) {
    const auto found = table_->findItems(name, Qt::MatchExactly, Name);
    if (found.isEmpty()) {
        return false;
    }
    table_->setCurrentItem(found.first());
    return true;
}

QStringList SelectProjectDialog::listedProjects() const {
    QStringList names;
    for (int i = 0; i < table_->topLevelItemCount(); ++i) {
        names << table_->topLevelItem(i)->text(Name);
    }
    return names;
}

} // namespace lumenis::ui
