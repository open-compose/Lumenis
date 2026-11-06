// The SubCap effect's dialogs (lumenis/ui/subcap_dialogs.h).
#include "lumenis/ui/subcap_dialogs.h"

#include "lumenis/model/subcap.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QInputDialog>

#include <functional>

namespace lumenis::ui {

QList<MasterCaptionListDialog::Entry> subcapEntries(const model::Sequence& sequence) {
    QList<MasterCaptionListDialog::Entry> out;
    for (const model::Track& t : sequence.tracks()) {
        if (t.kind != model::TrackKind::Video) continue;
        std::int64_t at = 0;
        for (const model::Segment& s : t.segments) {
            const std::int64_t start = at;
            at += s.length;
            if (s.effect && model::isSubCap(*s.effect)) out << MasterCaptionListDialog::Entry{{t.name(), start}, at, model::captionText(*s.effect)};
        }
    }
    return out;
}

MasterCaptionListDialog::MasterCaptionListDialog(const QList<Entry>& entries, const TimecodeFormat& format, std::int64_t startFrame,
                                                 QWidget* parent)
    : QDialog(parent), entries_(entries) {
    setObjectName(QStringLiteral("masterCaptionList"));
    setWindowTitle(tr("Edit Master Caption List"));
    auto* layout = new QVBoxLayout(this);
    table_ = new QTableWidget(static_cast<int>(entries.size()), 3, this);
    table_->setObjectName(QStringLiteral("masterCaptionTable"));
    table_->setHorizontalHeaderLabels({tr("Track"), tr("Timecode"), tr("Caption")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int i = 0; i < entries.size(); ++i) {
        const Entry& e = entries.at(i);
        table_->setItem(i, 0, new QTableWidgetItem(e.segment.track));
        table_->setItem(i, 1, new QTableWidgetItem(QStringLiteral("%1 - %2").arg(
                                    QString::fromStdString(Timecode(startFrame + e.segment.start, format).toString()),
                                    QString::fromStdString(Timecode(startFrame + e.end, format).toString()))));
        table_->setItem(i, 2, new QTableWidgetItem(QString(e.text).replace(QLatin1Char('\n'), QStringLiteral(" / "))));
    }
    layout->addWidget(table_, 1);
    // "For each caption you want to edit, modify the text in the caption text box. Press Enter (Windows) or Return
    // (Macintosh) to enter a line break" (p.384).
    layout->addWidget(new QLabel(tr("Caption text:"), this));
    text_ = new QPlainTextEdit(this);
    text_->setObjectName(QStringLiteral("masterCaptionText"));
    text_->setMaximumHeight(90);
    layout->addWidget(text_);
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row) { showRow(row); });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        if (shown_ < 0 || shown_ >= entries_.size()) return;
        entries_[shown_].text = text_->toPlainText();
        table_->item(shown_, 2)->setText(QString(entries_[shown_].text).replace(QLatin1Char('\n'), QStringLiteral(" / ")));
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(640, 420);
    if (!entries_.isEmpty()) table_->setCurrentCell(0, 0);
}

void MasterCaptionListDialog::showRow(int row) {
    shown_ = -1;
    text_->setPlainText(row >= 0 && row < entries_.size() ? entries_.at(row).text : QString());
    text_->setEnabled(row >= 0 && row < entries_.size());
    shown_ = row;
}

QList<MasterCaptionListDialog::Entry> MasterCaptionListDialog::entries() const { return entries_; }

void MasterCaptionListDialog::setText(int row, const QString& text) {
    table_->setCurrentCell(row, 0);
    text_->setPlainText(text);
}

ExportCaptionsDialog::ExportCaptionsDialog(const QStringList& tracks, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("exportCaptions"));
    setWindowTitle(tr("Export Caption Data"));
    auto* layout = new QVBoxLayout(this);
    // "you can select which caption tracks to include in the export, and you can determine how the export resolves
    // conflicts between captions on different tracks that overlap in time" (p.386).
    layout->addWidget(new QLabel(tr("Caption tracks:"), this));
    tracks_ = new QListWidget(this);
    tracks_->setObjectName(QStringLiteral("exportCaptionTracks"));
    for (const QString& t : tracks) {
        auto* item = new QListWidgetItem(t, tracks_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    layout->addWidget(tracks_);
    layout->addWidget(new QLabel(tr("Where captions overlap:"), this));
    conflict_ = new QButtonGroup(this);
    auto* upper = new QRadioButton(tr("Use the higher track's caption"), this);
    upper->setObjectName(QStringLiteral("exportCaptionsUpper"));
    upper->setChecked(true);
    auto* lower = new QRadioButton(tr("Use the lower track's caption"), this);
    lower->setObjectName(QStringLiteral("exportCaptionsLower"));
    conflict_->addButton(upper, 1);
    conflict_->addButton(lower, 0);
    layout->addWidget(upper);
    layout->addWidget(lower);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QStringList ExportCaptionsDialog::tracks() const {
    QStringList out;
    for (int i = 0; i < tracks_->count(); ++i) {
        if (tracks_->item(i)->checkState() == Qt::Checked) out << tracks_->item(i)->text();
    }
    return out;
}

bool ExportCaptionsDialog::topWins() const { return conflict_->checkedId() != 0; }

void ExportCaptionsDialog::setTracks(const QStringList& tracks) {
    for (int i = 0; i < tracks_->count(); ++i) tracks_->item(i)->setCheckState(tracks.contains(tracks_->item(i)->text()) ? Qt::Checked : Qt::Unchecked);
}

void ExportCaptionsDialog::setTopWins(bool top) { conflict_->button(top ? 1 : 0)->setChecked(true); }

// ---------------------------------------------------------------- Global Properties (7.4a2)

namespace {

const QString kProps = QString::fromLatin1(model::kSubCapPropertiesCategory);
const QString kParams = QString::fromLatin1(model::kSubCapCategory);

// The text items and their limits (p.504: "Enter up to 32 characters"; Custom "up to 576").
const QList<QPair<QString, QString>>& textFields() {
    static const QList<QPair<QString, QString>> all{
        {QStringLiteral("Publisher"), QStringLiteral("Contacts")},
        {QStringLiteral("Editor's Name"), QStringLiteral("Contacts")},
        {QStringLiteral("Editor's Contact Details"), QStringLiteral("Contacts")},
        {QStringLiteral("Translator's Name"), QStringLiteral("Contacts")},
        {QStringLiteral("Translator's Contact Details"), QStringLiteral("Contacts")},
        {QStringLiteral("Original Program Title"), QStringLiteral("Program")},
        {QStringLiteral("Original Episode Title"), QStringLiteral("Program")},
        {QStringLiteral("Translated Program Title"), QStringLiteral("Program")},
        {QStringLiteral("Translated Episode Title"), QStringLiteral("Program")},
    };
    return all;
}

// The items the Synchronize tab lists: every SubCap parameter but the caption text, then the information items.
QStringList syncItems() {
    static const QStringList items{
        QStringLiteral("Bypass Effect"), QStringLiteral("Color"), QStringLiteral("Opacity"), QStringLiteral("Font Face"),
        QStringLiteral("Font Size"), QStringLiteral("Line Weight"), QStringLiteral("Outline Weight"), QStringLiteral("Outline"),
        QStringLiteral("Text Appearance: Color"), QStringLiteral("Text Shadow"), QStringLiteral("Radius"), QStringLiteral("Angle"),
        QStringLiteral("Text Alignment"), QStringLiteral("Box Appearance: Color"), QStringLiteral("Box Appearance: Opacity"),
        QStringLiteral("Box Style"), QStringLiteral("Anchor Box to"), QStringLiteral("Justify Anchor"), QStringLiteral("X"),
        QStringLiteral("Y"), QStringLiteral("Box Width"), QStringLiteral("Box Width thumbwheel"), QStringLiteral("Number of Rows of Text"),
        QStringLiteral("Row Count"), QStringLiteral("Vertical"), QStringLiteral("Horizontal"), QStringLiteral("Row Spacing"),
        QStringLiteral("Publisher"), QStringLiteral("Country of Origin"), QStringLiteral("Editor's Name"),
        QStringLiteral("Editor's Contact Details"), QStringLiteral("Translator's Name"), QStringLiteral("Translator's Contact Details"),
        QStringLiteral("Custom"), QStringLiteral("Revision Date"), QStringLiteral("Creation Date"), QStringLiteral("Language"),
        QStringLiteral("Original Program Title"), QStringLiteral("Original Episode Title"), QStringLiteral("Translated Program Title"),
        QStringLiteral("Translated Episode Title")};
    return items;
}

} // namespace

QList<QPair<QString, QString>> SubcapPropertiesDialog::countries() {
    // "Select a country name from the list. (Media Composer converts the name to a standard country code in a .stl
    // file.)" Ours: a short list with ISO 3166 three-letter codes.
    return {{tr("(none)"), QString()}, {tr("Australia"), QStringLiteral("AUS")}, {tr("Austria"), QStringLiteral("AUT")},
            {tr("Belgium"), QStringLiteral("BEL")}, {tr("Canada"), QStringLiteral("CAN")}, {tr("Denmark"), QStringLiteral("DNK")},
            {tr("Finland"), QStringLiteral("FIN")}, {tr("France"), QStringLiteral("FRA")}, {tr("Germany"), QStringLiteral("DEU")},
            {tr("Ireland"), QStringLiteral("IRL")}, {tr("Italy"), QStringLiteral("ITA")}, {tr("Netherlands"), QStringLiteral("NLD")},
            {tr("New Zealand"), QStringLiteral("NZL")}, {tr("Norway"), QStringLiteral("NOR")}, {tr("Poland"), QStringLiteral("POL")},
            {tr("Portugal"), QStringLiteral("PRT")}, {tr("Spain"), QStringLiteral("ESP")}, {tr("Sweden"), QStringLiteral("SWE")},
            {tr("Switzerland"), QStringLiteral("CHE")}, {tr("United Kingdom"), QStringLiteral("GBR")},
            {tr("United States"), QStringLiteral("USA")}};
}

QList<QPair<QString, QString>> SubcapPropertiesDialog::languages() {
    // "Select a language from the list. (Media Composer converts the language name to a standard language code in a .stl
    // file.)" The codes of EBU Tech 3264's language table.
    return {{tr("(unknown)"), QStringLiteral("00")}, {tr("Danish"), QStringLiteral("07")}, {tr("German"), QStringLiteral("08")},
            {tr("English"), QStringLiteral("09")},   {tr("Spanish"), QStringLiteral("0A")}, {tr("French"), QStringLiteral("0F")},
            {tr("Italian"), QStringLiteral("15")},   {tr("Dutch"), QStringLiteral("1D")},   {tr("Norwegian"), QStringLiteral("1E")},
            {tr("Polish"), QStringLiteral("20")},    {tr("Portuguese"), QStringLiteral("21")}, {tr("Finnish"), QStringLiteral("27")},
            {tr("Swedish"), QStringLiteral("28")},   {tr("Turkish"), QStringLiteral("29")}};
}

SubcapPropertiesDialog::SubcapPropertiesDialog(const model::Effect& effect, const QString& folder, QWidget* parent)
    : QDialog(parent), effect_(effect), folder_(folder) {
    setObjectName(QStringLiteral("subcapProperties"));
    setWindowTitle(tr("Global Properties"));
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    QMap<QString, QFormLayout*> forms;
    for (const QString& tab : {QStringLiteral("Contacts"), QStringLiteral("Custom"), QStringLiteral("Program")}) {
        auto* page = new QWidget(tabs);
        forms.insert(tab, new QFormLayout(page));
        tabs->addTab(page, tab == QLatin1String("Contacts") ? tr("Contacts") : tab == QLatin1String("Custom") ? tr("Custom") : tr("Program"));
    }
    for (const auto& [name, tab] : textFields()) {
        auto* edit = new QLineEdit(this);
        edit->setObjectName(QStringLiteral("subcapField:") + name);
        edit->setMaxLength(32);
        texts_.insert(name, edit);
        forms.value(tab)->addRow(name + QLatin1Char(':'), edit);
        if (name == QLatin1String("Publisher")) {
            country_ = new QComboBox(this);
            country_->setObjectName(QStringLiteral("subcapField:Country of Origin"));
            for (const auto& [n, code] : countries()) country_->addItem(n, code);
            forms.value(tab)->addRow(tr("Country of Origin:"), country_);
        }
    }
    custom_ = new QPlainTextEdit(this);
    custom_->setObjectName(QStringLiteral("subcapField:Custom"));
    forms.value(QStringLiteral("Custom"))->addRow(custom_);
    revision_ = new QDateEdit(this);
    revision_->setObjectName(QStringLiteral("subcapField:Revision Date"));
    revision_->setCalendarPopup(true);
    creation_ = new QDateEdit(this);
    creation_->setObjectName(QStringLiteral("subcapField:Creation Date"));
    creation_->setCalendarPopup(true);
    language_ = new QComboBox(this);
    language_->setObjectName(QStringLiteral("subcapField:Language"));
    for (const auto& [n, code] : languages()) language_->addItem(n, code);
    QFormLayout* program = forms.value(QStringLiteral("Program"));
    program->insertRow(0, tr("Revision Date:"), revision_);
    program->insertRow(1, tr("Creation Date:"), creation_);
    program->insertRow(2, tr("Language:"), language_);

    // Stylesheets (p.387-388): Create, Delete, Apply, Set Default.
    auto* styles = new QWidget(tabs);
    auto* styleLayout = new QHBoxLayout(styles);
    stylesheets_ = new QListWidget(styles);
    stylesheets_->setObjectName(QStringLiteral("subcapStylesheets"));
    styleLayout->addWidget(stylesheets_, 1);
    auto* styleButtons = new QVBoxLayout;
    auto button = [&](const QString& text, const char* name, std::function<void()> act) {
        auto* b = new QPushButton(text, styles);
        b->setObjectName(QString::fromLatin1(name));
        connect(b, &QPushButton::clicked, this, std::move(act));
        styleButtons->addWidget(b);
    };
    auto selected = [this] { return stylesheets_->currentItem() ? stylesheets_->currentItem()->text().remove(QStringLiteral(" (default)")) : QString(); };
    button(tr("Create"), "subcapStyleCreate", [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Add New Stylesheet"), tr("Stylesheet Name:"), QLineEdit::Normal, QString(), &ok);
        if (ok) createStylesheet(name.trimmed());
    });
    button(tr("Delete"), "subcapStyleDelete", [this, selected] { deleteStylesheet(selected()); });
    button(tr("Apply"), "subcapStyleApply", [this, selected] { applyStylesheet(selected()); });
    button(tr("Set Default"), "subcapStyleDefault", [this, selected] { setDefaultStylesheet(selected()); });
    styleButtons->addStretch(1);
    styleLayout->addLayout(styleButtons);
    tabs->addTab(styles, tr("Stylesheets"));

    // Synchronize (p.384-385).
    auto* syncPage = new QWidget(tabs);
    auto* syncForm = new QFormLayout(syncPage);
    for (const QString& item : syncItems()) {
        auto* mode = new QComboBox(syncPage);
        mode->setObjectName(QStringLiteral("subcapSync:") + item);
        mode->addItems({QStringLiteral("Clip"), QStringLiteral("Track"), QStringLiteral("Sequence")});
        sync_.insert(item, mode);
        syncForm->addRow(item + QLatin1Char(':'), mode);
    }
    tabs->addTab(syncPage, tr("Synchronize"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    fill();
    refreshStylesheets();
}

void SubcapPropertiesDialog::fill() {
    const QJsonObject p = effect_.parameters.value(kProps).toObject();
    for (auto it = texts_.begin(); it != texts_.end(); ++it) it.value()->setText(p.value(it.key()).toString());
    country_->setCurrentIndex(std::max(0, country_->findData(p.value(QStringLiteral("Country of Origin")).toString())));
    language_->setCurrentIndex(std::max(0, language_->findData(p.value(QStringLiteral("Language")).toString())));
    custom_->setPlainText(p.value(QStringLiteral("Custom")).toString());
    const QDate today = QDate::currentDate();
    const QDate created = QDate::fromString(p.value(QStringLiteral("Creation Date")).toString(), Qt::ISODate);
    const QDate revised = QDate::fromString(p.value(QStringLiteral("Revision Date")).toString(), Qt::ISODate);
    creation_->setDate(created.isValid() ? created : today);
    revision_->setDate(revised.isValid() ? revised : today);
    for (auto it = sync_.begin(); it != sync_.end(); ++it) it.value()->setCurrentText(model::subcapSyncMode(effect_, it.key()));
}

model::Effect SubcapPropertiesDialog::effect() const {
    model::Effect e = effect_;
    QJsonObject p = e.parameters.value(kProps).toObject();
    for (auto it = texts_.begin(); it != texts_.end(); ++it) p.insert(it.key(), it.value()->text().left(32));
    p.insert(QStringLiteral("Country of Origin"), country_->currentData().toString());
    p.insert(QStringLiteral("Language"), language_->currentData().toString());
    p.insert(QStringLiteral("Custom"), custom_->toPlainText().left(576));
    p.insert(QStringLiteral("Creation Date"), creation_->date().toString(Qt::ISODate));
    p.insert(QStringLiteral("Revision Date"), revision_->date().toString(Qt::ISODate));
    QJsonObject sync;
    for (auto it = sync_.begin(); it != sync_.end(); ++it) sync.insert(it.key(), it.value()->currentText());
    p.insert(QLatin1String(model::kSynchronizeKey), sync);
    e.parameters.insert(kProps, p);
    return e;
}

void SubcapPropertiesDialog::setField(const QString& name, const QString& value) {
    if (QLineEdit* edit = texts_.value(name)) edit->setText(value);
    else if (name == QLatin1String("Country of Origin")) country_->setCurrentIndex(std::max(0, country_->findData(value)));
    else if (name == QLatin1String("Language")) language_->setCurrentIndex(std::max(0, language_->findData(value)));
    else if (name == QLatin1String("Custom")) custom_->setPlainText(value);
    else if (name == QLatin1String("Creation Date")) creation_->setDate(QDate::fromString(value, Qt::ISODate));
    else if (name == QLatin1String("Revision Date")) revision_->setDate(QDate::fromString(value, Qt::ISODate));
}

void SubcapPropertiesDialog::setSync(const QString& name, const QString& mode) {
    if (QComboBox* c = sync_.value(name)) c->setCurrentText(mode);
}

QStringList SubcapPropertiesDialog::stylesheets() const {
    QStringList out;
    for (const QString& f : QDir(folder_).entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name)) out << QFileInfo(f).completeBaseName();
    return out;
}

void SubcapPropertiesDialog::refreshStylesheets() {
    stylesheets_->clear();
    const QString preferred = defaultSubcapStylesheet(folder_);
    for (const QString& name : stylesheets()) stylesheets_->addItem(name == preferred ? name + QStringLiteral(" (default)") : name);
}

bool SubcapPropertiesDialog::createStylesheet(const QString& name) {
    if (name.isEmpty() || folder_.isEmpty()) return false;
    QDir().mkpath(folder_);
    // The stylesheet holds the dialog's information too, as it now stands.
    const bool ok = writeSubcapStylesheet(QDir(folder_).filePath(name + QStringLiteral(".xml")), effect());
    refreshStylesheets();
    return ok;
}

bool SubcapPropertiesDialog::applyStylesheet(const QString& name) {
    if (name.isEmpty()) return false;
    model::Effect e = effect();
    if (!applySubcapStylesheet(QDir(folder_).filePath(name + QStringLiteral(".xml")), e)) return false;
    effect_ = e;
    fill();
    return true;
}

bool SubcapPropertiesDialog::deleteStylesheet(const QString& name) {
    if (name.isEmpty()) return false;
    const bool ok = QFile::remove(QDir(folder_).filePath(name + QStringLiteral(".xml")));
    if (defaultSubcapStylesheet(folder_) == name) QFile::remove(QDir(folder_).filePath(QStringLiteral("default.txt")));
    refreshStylesheets();
    return ok;
}

bool SubcapPropertiesDialog::setDefaultStylesheet(const QString& name) {
    if (name.isEmpty() || !stylesheets().contains(name)) return false;
    QSaveFile f(QDir(folder_).filePath(QStringLiteral("default.txt")));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(name.toUtf8());
    const bool ok = f.commit();
    refreshStylesheets();
    return ok;
}

bool writeSubcapStylesheet(const QString& path, const model::Effect& effect) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QXmlStreamWriter xml(&f);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("CaptionStylesheet"));
    xml.writeAttribute(QStringLiteral("name"), QFileInfo(path).completeBaseName());
    for (const QString& category : {kParams, kProps}) {
        const QJsonObject values = effect.parameters.value(category).toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (it.key() == QLatin1String(model::kCaptionTextKey)) continue;
            xml.writeStartElement(QStringLiteral("Value"));
            xml.writeAttribute(QStringLiteral("category"), category);
            xml.writeAttribute(QStringLiteral("name"), it.key());
            // The value as JSON, so that numbers, text and the Synchronize choices come back as they were.
            xml.writeCharacters(QString::fromUtf8(QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact)));
            xml.writeEndElement();
        }
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return f.commit();
}

bool applySubcapStylesheet(const QString& path, model::Effect& effect) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QXmlStreamReader xml(&f);
    bool any = false;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1String("Value")) continue;
        const QString category = xml.attributes().value(QStringLiteral("category")).toString();
        const QString name = xml.attributes().value(QStringLiteral("name")).toString();
        const QJsonArray wrapped = QJsonDocument::fromJson(xml.readElementText().toUtf8()).array();
        if ((category != kParams && category != kProps) || name == QLatin1String(model::kCaptionTextKey) || wrapped.isEmpty()) continue;
        QJsonObject values = effect.parameters.value(category).toObject();
        values.insert(name, wrapped.first());
        effect.parameters.insert(category, values);
        any = true;
    }
    return !xml.hasError() && any;
}

QString defaultSubcapStylesheet(const QString& folder) {
    QFile f(QDir(folder).filePath(QStringLiteral("default.txt")));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

} // namespace lumenis::ui
