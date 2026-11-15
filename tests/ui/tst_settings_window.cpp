#include "lumenis/ext/provider.h"
#include "lumenis/media/managed.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/safe_color_graph.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/workspace.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using model::SettingEntry;
using model::SettingsFile;
using model::SettingsScope;

namespace {

const SettingKind& kind(const char* name) {
    const SettingKind* k = SettingsSchema::builtIn().find(QString::fromLatin1(name));
    Q_ASSERT(k);
    return *k;
}

// A sequence with two segments on V1, A1 and A2, 50 frames each.
model::BinItem sequence() {
    model::BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Cut");
    s.kind = model::BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    for (const QString& t : seq.trackNames()) {
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.name = QStringLiteral("A");
        a.sourceTrack = t;
        a.length = 50;
        model::Segment b = a;
        b.clipId = QStringLiteral("b");
        seq.splice(t, 0, {a, b});
    }
    seq.storeIn(s);
    return s;
}

} // namespace

class TestSettingsWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void schemaIsComplete() {
        // Every setting has a scope and a page, every option a page, and a
        // choice's default is one of its choices (guide ch.33).
        const SettingsSchema& schema = SettingsSchema::builtIn();
        QCOMPARE(schema.kindNames(SettingsScope::Project),
                 (QStringList{"Audio Project", "Video Output", "Audio Multi-Mix", "Dynamic Relink", "General", "Media Creation", "S3D", "Safe Colors", "Bin Layout", "Transcript", "Video Display"}));
        QCOMPARE(schema.kindNames(SettingsScope::User),
                 (QStringList{"Audio", "Bin", "Color Management", "Composer", "Correction", "Effect Editor", "Export", "Full Screen Playback", "Import", "Interface", "Keyboard", "Link",
                              "Render", "Script", "Timeline", "Timeline View", "Title Style", "Tool Palette", "Workspace View", "Trim"}));
        QSet<QString> keys;
        for (const SettingKind& k : schema.kinds()) {
            // Bin layouts are Project settings the Settings chapter does not
            // list; the Bin Layout menu's pages describe them (p.92). Title
            // styles are the FX Guide's (p.364); the Transcript setting is in the PhraseFind AI chapter (p.447).
            QVERIFY2(k.page >= 1136 || k.kind == QLatin1String("Bin Layout") || k.kind == QLatin1String("Title Style") ||
                         k.kind == QLatin1String("Transcript"),
                     qPrintable(k.kind));
            // Without tabs a setting opens a tool instead (the Tool Palette).
            QVERIFY2(!k.tabs.isEmpty() || !k.opens.isEmpty(), qPrintable(k.kind));
            for (const SettingTab& t : k.tabs) {
                for (const SettingOption& o : t.options) {
                    const QString where = k.kind + QLatin1Char('.') + o.key;
                    // Its setting's pages, or another chapter (Show Labels in
                    // Tool Palette is on p.392), or the FX Guide's (Use Classic
                    // Character Mapping, FX p.377). The Export setting's MOV and MP4 options are described in the
                    // Exporting chapter (p.879-900, 7.1a-b) and the Export Settings reference runs to p.1187. The DCP tab's
                    // options are ours and cite the guide's only mention of DCP output (p.519, 7.5).
                    const bool exportChapter =
                        k.kind == QLatin1String("Export") &&
                        ((o.page >= 879 && o.page <= 900) || (o.page >= 1173 && o.page <= 1187) || (o.key.startsWith(QLatin1String("dcp")) && o.page == 519));
                    // Transcript's Manage Speakers is described with the Transcript Tool (p.445), in the same chapter.
                    const bool transcriptChapter = k.kind == QLatin1String("Transcript") && o.page >= 436 && o.page <= 453;
                    // Color Management's Installed LUTs are described with the Color Encoding tab (p.539).
                    const bool colorChapter = k.kind == QLatin1String("Color Management") && o.page >= 536 && o.page <= 544;
                    // Import's OMFI/AAF report options are described with AAF import, "Sequence Report for AAF Imports"
                    // (p.237-238; 9.2c).
                    const bool aafReport = k.kind == QLatin1String("Import") && o.page >= 237 && o.page <= 238;
                    // Media Creation's Single/Dual Drives Mode button is described with capturing (p.135).
                    const bool driveMode = k.kind == QLatin1String("Media Creation") && o.page == 135;
                    // Ours, where the guides say nothing (the interface's language, 10.4): no page, marked "ours".
                    QVERIFY2((o.page >= k.page && o.page <= k.page + 10) || o.page == 392 || (o.page >= 89 && o.page <= 92) || o.fxGuide ||
                                 exportChapter || transcriptChapter || colorChapter || aafReport || driveMode || (o.ours && o.page == 0),
                             qPrintable(where));
                    QVERIFY2(!keys.contains(where), qPrintable(where));
                    keys << where;
                    QVERIFY2(QStringList({"bool", "int", "number", "choice", "timecode", "text", "info", "drive", "color", "font", "safeColorGraph", "button",
                                         "applyToAll", "driveMode"}).contains(o.type),
                             qPrintable(where));
                    // An Apply to All button names what it copies.
                    if (o.type == QLatin1String("applyToAll")) {
                        QVERIFY2(o.applies == QLatin1String("resolution") || o.applies == QLatin1String("drives"), qPrintable(where));
                    }
                    if (o.type == QLatin1String("choice")) {
                        bool found = false;
                        for (const SettingChoice& c : o.choices) found = found || (c.value == o.defaultValue.toString() && !c.later);
                        QVERIFY2(found, qPrintable(where));
                    }
                    if (o.type == QLatin1String("int")) {
                        QVERIFY2(o.defaultValue.toInt() >= o.min && o.defaultValue.toInt() <= o.max, qPrintable(where));
                    }
                    if (o.type == QLatin1String("number")) {
                        QVERIFY2(o.defaultValue.toDouble() >= o.minNumber && o.defaultValue.toDouble() <= o.maxNumber, qPrintable(where));
                    }
                }
            }
        }
    }

    void userAndProjectFiles() {
        // User settings in the user's folder, Project settings in the
        // project folder (p.1136).
        QTemporaryDir dir;
        Settings settings;
        settings.setUser(dir.filePath("Users"), QStringLiteral("ed"));
        QVERIFY(QFile::exists(dir.filePath("Users/ed/Settings.json")));
        settings.setProject(dir.filePath("Project"));
        QVERIFY(QFile::exists(dir.filePath("Project/Settings.json")));
        QCOMPARE(settings.number("Bin", "autoSaveMinutes"), 15);
        settings.setValue("Bin", "autoSaveMinutes", 5);
        settings.setValue("General", "defaultStartTc", QStringLiteral("10:00:00:00"));

        Settings again;
        again.setUser(dir.filePath("Users"), QStringLiteral("ed"));
        again.setProject(dir.filePath("Project"));
        QCOMPARE(again.number("Bin", "autoSaveMinutes"), 5);
        QCOMPARE(again.text("General", "defaultStartTc"), QStringLiteral("10:00:00:00"));
        // Another user has their own.
        again.setUser(dir.filePath("Users"), QStringLiteral("jo"));
        QCOMPARE(again.number("Bin", "autoSaveMinutes"), 15);
    }

    void newUsersStartFromSiteSettings() {
        // p.1141: new users and projects use the site settings.
        QTemporaryDir dir;
        SettingsFile site;
        site.entries << SettingEntry{QStringLiteral("Bin"), QStringLiteral("Site"), true,
                                     {{QStringLiteral("autoSaveMinutes"), 3}}};
        QVERIFY(site.write(dir.filePath("Settings/Settings.json")));
        Settings settings;
        settings.setSiteFolder(dir.filePath("Settings"));
        settings.setUser(dir.filePath("Users"), QStringLiteral("new"));
        QCOMPARE(settings.number("Bin", "autoSaveMinutes"), 3);
        QCOMPARE(settings.file(SettingsScope::User).entries.at(
                     settings.file(SettingsScope::User).activeIndex("Bin")).name,
                 QStringLiteral("Site"));
    }

    void unavailableOptionsKeepTheirDefaults() {
        Settings settings;
        settings.setValue("Interface", "launchLastProject", true);   // later
        settings.setValue("Bin", "doubleClickLoads", QStringLiteral("popup")); // later choice
        settings.setValue("Bin", "autoSaveMinutes", QStringLiteral("x")); // wrong type
        QCOMPARE(settings.flag("Interface", "launchLastProject"), false);
        QCOMPARE(settings.text("Bin", "doubleClickLoads"), QStringLiteral("monitor"));
        QCOMPARE(settings.number("Bin", "autoSaveMinutes"), 15);
        QVERIFY(settings.value("Nope", "nothing").isNull());
    }

    void safeColorsDialog() {
        // The Safe Colors setting (Editing Guide p.1216-1217; FX Guide p.721-725; 6.10e4a): limits with two decimals,
        // the defaults of FX Guide p.723's picture, and the graph following the dialog.
        SettingEntry entry{QStringLiteral("Safe Colors"), QString(), true, {}};
        SettingsDialog dialog(kind("Safe Colors"), entry);
        auto* high = qobject_cast<QDoubleSpinBox*>(dialog.editor("lumaHigh"));
        QVERIFY(high);
        QCOMPARE(high->decimals(), 2);
        QCOMPARE(high->value(), 108.45);
        auto* graph = qobject_cast<SafeColorGraph*>(dialog.editor("graph"));
        QVERIFY(graph);
        QCOMPARE(graph->limits(), model::SafeColorLimits{});
        high->setValue(100.0);
        auto* rgbAction = qobject_cast<QComboBox*>(dialog.editor("rgbAction"));
        QVERIFY(rgbAction);
        rgbAction->setCurrentIndex(rgbAction->findData(QStringLiteral("warn")));
        QCOMPARE(graph->limits().luma.high, 100.0);
        const model::SafeColorLimits chosen = model::SafeColorLimits::fromValues(dialog.values());
        QVERIFY(chosen.warnRgb && !chosen.warnCompositeLuma);
        QCOMPARE(chosen.luma.high, 100.0);
        QVERIFY(!dialog.values().contains(QStringLiteral("graph")));
        // The graph's bars: the left one to the low limit, the right one from the high limit.
        QVERIFY(SafeColorGraph::xOf(16.0, 300) < SafeColorGraph::xOf(235.0, 300));
        QCOMPARE(SafeColorGraph::xOf(-1000.0, 300), 0.0);
        QCOMPARE(SafeColorGraph::xOf(1000.0, 300), 300.0);
    }

    // Media Creation's Apply to All (p.135, p.1207): one tab's Video Resolution goes to every resolution menu, its video
    // and audio drives to every video and audio drive menu (the Motion Effects tab's video drive too); the buttons keep
    // no value of their own.
    void mediaCreationApplyToAll() {
        QTemporaryDir dir;
        const QString first = dir.filePath(QStringLiteral("First"));
        const QString second = dir.filePath(QStringLiteral("Second"));
        QVERIFY(QDir().mkpath(first) && QDir().mkpath(second));
        media::setExtraMediaDrives({first, second});
        SettingEntry entry{QStringLiteral("Media Creation"), QString(), true, {}};
        SettingsDialog dialog(kind("Media Creation"), entry);
        auto menu = [&](const char* key) { return qobject_cast<QComboBox*>(dialog.editor(key)); };
        QVERIFY(menu("importResolution") && menu("transcodeResolution") && menu("renderResolution"));
        menu("importResolution")->setCurrentIndex(menu("importResolution")->findData(QStringLiteral("dnx_lb")));
        QVERIFY(menu("transcodeResolution")->currentData().toString() != QLatin1String("dnx_lb"));
        auto* resolution = qobject_cast<QPushButton*>(dialog.editor("importApplyResolution"));
        QVERIFY(resolution && resolution->isEnabledTo(&dialog));
        QCOMPARE(resolution->text(), QStringLiteral("Apply to All"));
        resolution->click();
        QCOMPARE(menu("transcodeResolution")->currentData().toString(), QStringLiteral("dnx_lb"));
        QCOMPARE(menu("renderResolution")->currentData().toString(), QStringLiteral("dnx_lb"));
        // Drives: the second folder for video, the first for audio.
        const int videoAt = menu("importVideoDrive")->findData(QDir::cleanPath(second));
        const int audioAt = menu("importAudioDrive")->findData(QDir::cleanPath(first));
        QVERIFY(videoAt >= 0 && audioAt >= 0);
        menu("importVideoDrive")->setCurrentIndex(videoAt);
        menu("importAudioDrive")->setCurrentIndex(audioAt);
        qobject_cast<QPushButton*>(dialog.editor("importApplyDrives"))->click();
        QCOMPARE(menu("transcodeVideoDrive")->currentData().toString(), QDir::cleanPath(second));
        QCOMPARE(menu("transcodeAudioDrive")->currentData().toString(), QDir::cleanPath(first));
        QCOMPARE(menu("motionEffectsVideoDrive")->currentData().toString(), QDir::cleanPath(second));
        QCOMPARE(menu("renderVideoDrive")->currentData().toString(), QDir::cleanPath(second));
        const QJsonObject values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("renderResolution")).toString(), QStringLiteral("dnx_lb"));
        QVERIFY(!values.contains(QStringLiteral("importApplyDrives")));
        // Single/Dual Drives Mode (p.135): dual at first; single hides the audio drive, which follows the video drive.
        auto* mode = qobject_cast<QToolButton*>(dialog.editor("importDualDrives"));
        QVERIFY(mode && mode->isChecked());
        mode->setChecked(false);
        QVERIFY(menu("importAudioDrive")->parentWidget()->isHidden());
        QCOMPARE(menu("importAudioDrive")->currentData().toString(), QDir::cleanPath(second));
        menu("importVideoDrive")->setCurrentIndex(audioAt);
        QCOMPARE(menu("importAudioDrive")->currentData().toString(), QDir::cleanPath(first));
        QCOMPARE(dialog.values().value(QStringLiteral("importDualDrives")).toBool(), false);
        mode->setChecked(true);
        QVERIFY(!menu("importAudioDrive")->parentWidget()->isHidden());
        media::setExtraMediaDrives({});
    }

    void dialogShowsAndReturnsValues() {
        SettingEntry entry{QStringLiteral("Timeline"), QStringLiteral("Editing"), true,
                           {{QStringLiteral("showToolbar"), false}, {QStringLiteral("custom"), 1}}};
        SettingsDialog dialog(kind("Timeline"), entry);
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Timeline Settings - Editing"));
        QVERIFY(dialog.tabs());
        QCOMPARE(dialog.tabs()->count(), 2);
        QCOMPARE(dialog.tabs()->tabText(0), QStringLiteral("Display"));
        auto* toolbar = qobject_cast<QCheckBox*>(dialog.editor("showToolbar"));
        QVERIFY(toolbar);
        QVERIFY(!toolbar->isChecked());
        // Every Timeline option is built now (Double-Click to Show Nesting last, FX Guide p.220).
        QVERIFY(dialog.editor("doubleClickNesting")->isEnabledTo(&dialog));
        QVERIFY(dialog.editor("showToolbar")->isEnabledTo(&dialog));
        toolbar->setChecked(true);
        dialog.findChild<QRadioButton*>(QStringLiteral("movementDuringPlay:scroll"))->setChecked(true);
        dialog.findChild<QRadioButton*>(QStringLiteral("defaultSegmentTool:insert"))->setChecked(true);
        const QJsonObject v = dialog.values();
        QCOMPARE(v.value("showToolbar").toBool(), true);
        QCOMPARE(v.value("movementDuringPlay").toString(), QStringLiteral("scroll"));
        QCOMPARE(v.value("defaultSegmentTool").toString(), QStringLiteral("insert"));
        QCOMPARE(v.value("custom").toInt(), 1);           // unknown values are kept
        QCOMPARE(v.value("doubleClickNesting").toBool(), true);
        // Options not available yet are dimmed and not stored.
        SettingsDialog iface(kind("Interface"), SettingEntry{QStringLiteral("Interface"), {}, true, {}});
        QVERIFY(iface.editor("launchLastProject"));
        QVERIFY(!iface.editor("launchLastProject")->isEnabledTo(&iface));
        QVERIFY(!iface.values().contains("launchLastProject"));

        // Number boxes, menus and timecodes.
        SettingsDialog bin(kind("Bin"), SettingEntry{QStringLiteral("Bin"), {}, true, {}});
        QVERIFY(!bin.tabs());
        QCOMPARE(bin.windowTitle(), QStringLiteral("Bin Settings"));
        auto* minutes = qobject_cast<QSpinBox*>(bin.editor("autoSaveMinutes"));
        QCOMPARE(minutes->value(), 15);
        minutes->setValue(30);
        auto* favorites = qobject_cast<QComboBox*>(bin.editor("favoritesAt"));
        favorites->setCurrentIndex(favorites->findData(QStringLiteral("bottom")));
        auto* loads = qobject_cast<QComboBox*>(bin.editor("doubleClickLoads"));
        auto* items = qobject_cast<QStandardItemModel*>(loads->model());
        QVERIFY(!items->item(loads->findData(QStringLiteral("popup")))->isEnabled());
        QCOMPARE(bin.values().value("autoSaveMinutes").toInt(), 30);
        QCOMPARE(bin.values().value("favoritesAt").toString(), QStringLiteral("bottom"));

        // The buffer sizes: "Recommended" (0) by default, and the rs button puts it back (p.1147).
        SettingsDialog audio(kind("Audio"), SettingEntry{QStringLiteral("Audio"), {}, true, {}});
        auto* buffer = qobject_cast<QSpinBox*>(audio.editor("playBuffer"));
        QVERIFY(buffer && buffer->isEnabledTo(&audio));
        QCOMPARE(buffer->value(), 0);
        QCOMPARE(buffer->text(), QStringLiteral("Recommended"));
        buffer->setValue(512);
        QCOMPARE(audio.values().value("playBuffer").toInt(), 512);
        auto* rs = audio.findChild<QToolButton*>(QStringLiteral("playBufferReset"));
        QVERIFY(rs);
        QCOMPARE(rs->text(), QStringLiteral("rs"));
        rs->click();
        QCOMPARE(buffer->value(), 0);
        QVERIFY(audio.editor("channelOrder")->isEnabledTo(&audio)); // the waveforms' order (U1a)

        SettingsDialog general(kind("General"), SettingEntry{QStringLiteral("General"), {}, true, {}});
        general.setInfo("projectFormat", QStringLiteral("1080p/25"));
        QCOMPARE(qobject_cast<QLabel*>(general.editor("projectFormat"))->text(), QStringLiteral("1080p/25"));
        auto* tc = qobject_cast<QLineEdit*>(general.editor("defaultStartTc"));
        QCOMPARE(tc->text(), QStringLiteral("01:00:00:00"));
        tc->setText(QStringLiteral("00:59:58:00"));
        QCOMPARE(general.values().value("defaultStartTc").toString(), QStringLiteral("00:59:58:00"));
        tc->setText(QStringLiteral("99:99:99:99"));
        QCOMPARE(general.values().value("defaultStartTc").toString(), QStringLiteral("01:00:00:00"));
        QVERIFY(!general.values().contains("projectFormat"));

        // Timecodes are checked at the project's rate: frame 29 exists at
        // 30 fps but not at 25; 00:01:00;00 does not exist in 29.97 drop.
        tc->setText(QStringLiteral("00:59:58:29"));
        QCOMPARE(general.values().value("defaultStartTc").toString(), QStringLiteral("00:59:58:29"));
        general.setEditRate(Rational(25));
        QCOMPARE(general.values().value("defaultStartTc").toString(), QStringLiteral("01:00:00:00"));
        tc->setText(QStringLiteral("00:59:58:24"));
        QCOMPARE(general.values().value("defaultStartTc").toString(), QStringLiteral("00:59:58:24"));
        general.setEditRate(Rational(30000, 1001));
        tc->setText(QStringLiteral("00:01:00;00"));
        QCOMPARE(general.values().value("defaultStartTc").toString(), QStringLiteral("01:00:00:00"));
        tc->setText(QStringLiteral("00:01:00;02"));
        QCOMPARE(general.values().value("defaultStartTc").toString(), QStringLiteral("00:01:00;02"));

        // The controls start from what Settings::value() would return: a
        // stored value of the wrong type, an unknown choice or a choice not
        // available yet shows the default.
        SettingsDialog wrong(kind("Bin"), SettingEntry{QStringLiteral("Bin"), {}, true,
                                                       {{QStringLiteral("autoSaveMinutes"), QStringLiteral("x")},
                                                        {QStringLiteral("favoritesAt"), QStringLiteral("nowhere")},
                                                        {QStringLiteral("doubleClickLoads"), QStringLiteral("popup")}}});
        QCOMPARE(qobject_cast<QSpinBox*>(wrong.editor("autoSaveMinutes"))->value(), 15);
        auto* wrongLoads = qobject_cast<QComboBox*>(wrong.editor("doubleClickLoads"));
        QCOMPARE(wrongLoads->currentData().toString(), kind("Bin").option("doubleClickLoads")->defaultValue.toString());
        auto* wrongFavorites = qobject_cast<QComboBox*>(wrong.editor("favoritesAt"));
        QCOMPARE(wrongFavorites->currentData().toString(), kind("Bin").option("favoritesAt")->defaultValue.toString());
    }

    void listGroupsVersionsWithActiveCheckmarks() {
        // Picture on p.1136: one row per setting; versions under it.
        Settings settings;
        SettingsList list(settings, SettingsScope::User);
        QCOMPARE(list.topLevelItemCount(), 20);
        QCOMPARE(list.topLevelItem(0)->text(SettingsList::kSettingColumn), QStringLiteral("Audio"));
        const int timeline = settings.file(SettingsScope::User).activeIndex("Timeline");
        list.selectEntries({timeline});
        list.duplicateSelection();
        QTreeWidgetItem* group = list.topLevelItem(14);
        QCOMPARE(group->text(0), QStringLiteral("Timeline"));
        QCOMPARE(group->childCount(), 2);
        QVERIFY(!group->data(0, SettingsList::kEntryRole).isValid());
        QCOMPARE(group->child(0)->checkState(0), Qt::Checked);
        QCOMPARE(group->child(1)->checkState(0), Qt::Unchecked);
        // The copy is selected; make it active (p.1139).
        const QList<int> copy = list.selectedEntries();
        QCOMPARE(copy.size(), 1);
        list.setActive(copy.first());
        QCOMPARE(settings.file(SettingsScope::User).activeIndex("Timeline"), copy.first());
        QCOMPARE(list.itemFor(copy.first())->checkState(0), Qt::Checked);
        // Checking a box makes that version active.
        list.itemFor(timeline)->setCheckState(0, Qt::Checked);
        QTRY_COMPARE(settings.file(SettingsScope::User).activeIndex("Timeline"), timeline);

        // Naming (p.1139), then duplicating a named setting (p.1139).
        list.rename(timeline, QStringLiteral("Edit"));
        QCOMPARE(list.itemFor(timeline)->text(SettingsList::kNameColumn), QStringLiteral("Edit"));
        list.selectEntries({timeline});
        list.duplicateSelection();
        QCOMPARE(settings.file(SettingsScope::User).entries.at(list.selectedEntries().first()).name,
                 QStringLiteral("Edit.1"));

        // Delete (p.1140): the versions go; a setting the scope needs comes
        // back with its defaults.
        list.selectEntries(settings.file(SettingsScope::User).versions("Timeline"));
        list.deleteSelection();
        QCOMPARE(settings.file(SettingsScope::User).versions("Timeline").size(), 1);
        QVERIFY(settings.file(SettingsScope::User).activeValues("Timeline").isEmpty());
    }

    void restoreToDefault() {
        // p.1140: Restore, or Copy & Restore.
        Settings settings;
        settings.setValue("Trim", "autoFocus", true);
        SettingsList list(settings, SettingsScope::User);
        const int trim = settings.file(SettingsScope::User).activeIndex("Trim");
        list.selectEntries({trim});
        list.restoreSelection(SettingsList::Restore::CopyAndRestore);
        const SettingsFile& f = settings.file(SettingsScope::User);
        QCOMPARE(f.versions("Trim").size(), 2);
        QVERIFY(f.activeValues("Trim").isEmpty());
        QCOMPARE(f.entries.at(f.versions("Trim").last()).values.value("autoFocus").toBool(), true);
        QVERIFY(!settings.flag("Trim", "autoFocus"));

        settings.setValue("Trim", "autoFocus", true);
        list.selectEntries({f.activeIndex("Trim")});
        // Asked first: Cancel leaves it.
        QTimer::singleShot(0, [] {
            if (auto* box = qobject_cast<QDialog*>(QApplication::activeModalWidget())) box->reject();
        });
        list.restoreSelection();
        QVERIFY(settings.flag("Trim", "autoFocus"));
        list.restoreSelection(SettingsList::Restore::Restore);
        QVERIFY(!settings.flag("Trim", "autoFocus"));
        QCOMPARE(f.versions("Trim").size(), 2);
    }

    void openingASettingEditsIt() {
        Settings settings;
        SettingsList list(settings, SettingsScope::User);
        const int composer = settings.file(SettingsScope::User).activeIndex("Composer");
        list.dialogHook = [](SettingsDialog& d) {
            QCOMPARE(d.tabs()->count(), 5); // Window, Edit, Move, MultiCam, Viewer (p.1158-1161)
            qobject_cast<QCheckBox*>(d.editor("activateRecordAfterEdit"))->setChecked(true);
            QTimer::singleShot(0, &d, &QDialog::accept);
        };
        QVERIFY(list.openEntry(composer));
        QVERIFY(settings.flag("Composer", "activateRecordAfterEdit"));
        list.dialogHook = [](SettingsDialog& d) { QTimer::singleShot(0, &d, &QDialog::reject); };
        QVERIFY(!list.openEntry(composer));
    }

    void copyingBetweenLists() {
        // p.1141: settings dragged to the site settings, or into a list.
        Settings settings;
        SettingsList user(settings, SettingsScope::User);
        SettingsList site(settings, SettingsScope::Site);
        SettingsList project(settings, SettingsScope::Project);
        settings.setValue("Bin", "autoSaveMinutes", 7);
        const SettingEntry bin = settings.file(SettingsScope::User).entries.at(
            settings.file(SettingsScope::User).activeIndex("Bin"));
        QCOMPARE(site.copyIn({bin}, false), 1);
        QCOMPARE(site.topLevelItemCount(), 1);
        // A User setting does not go into the Project list.
        QCOMPARE(project.copyIn({bin}, false), 0);
        // Add keeps the current version; Replace overwrites it.
        SettingEntry other = bin;
        other.values.insert("autoSaveMinutes", 9);
        QCOMPARE(user.copyIn({other}, false), 1);
        QCOMPARE(settings.number("Bin", "autoSaveMinutes"), 7);
        user.copyIn({other}, true);
        QCOMPARE(settings.number("Bin", "autoSaveMinutes"), 9);
    }

    void windowTabsAndUserProfiles() {
        QTemporaryDir dir;
        model::UserProfiles(dir.filePath("Users")).create(QStringLiteral("ann"));
        Settings settings;
        settings.setUser(dir.filePath("Users"), QStringLiteral("ann"));
        SettingsWindow window(settings);
        QCOMPARE(window.windowTitle(), QStringLiteral("Settings"));
        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("settingsWindowTabs"));
        QCOMPARE(tabs->count(), 4);
        QCOMPARE(tabs->tabText(0), QStringLiteral("Format"));
        QCOMPARE(tabs->tabText(3), QStringLiteral("Site"));
        QCOMPARE(window.currentTab(), SettingsWindow::Tab::User);
        QCOMPARE(window.currentList(), window.list(SettingsScope::User));
        // No project: the Project tab is off and Format says so.
        QVERIFY(!tabs->isTabEnabled(1));

        model::ProjectEntry entry;
        entry.settings.name = QStringLiteral("Film");
        entry.settings.format = projectFormatPresets().front();
        window.setProject(entry);
        QVERIFY(tabs->isTabEnabled(1));
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("formatProjectName"))->text(), QStringLiteral("Film"));
        // Project Type: a menu of the formats with the project's edit rate (p.1287-1288), the project's first.
        auto* type = window.findChild<QComboBox*>(QStringLiteral("formatProjectType"));
        QVERIFY(type);
        QCOMPARE(type->currentText(), QString::fromStdString(entry.settings.format.label()));
        for (int i = 0; i < type->count(); ++i) {
            const auto same = std::find_if(projectFormatPresets().begin(), projectFormatPresets().end(), [&](const ProjectFormat& f) {
                return f.label() == type->itemText(i).toStdString();
            });
            QVERIFY(same != projectFormatPresets().end());
            QCOMPARE(same->editRate, entry.settings.format.editRate);
        }

        // The User Profile menu (p.1143-1145).
        QComboBox* profiles = window.userProfiles();
        QCOMPARE(profiles->currentText(), QStringLiteral("ann"));
        QVERIFY(profiles->findData(QStringLiteral("create")) >= 0);
        QVERIFY(profiles->findData(QStringLiteral("reveal")) >= 0);
        QVERIFY(window.createUser(QStringLiteral("bob")));
        QCOMPARE(settings.user(), QStringLiteral("bob"));
        QVERIFY(QFile::exists(dir.filePath("Users/bob/Settings.json")));
        QCOMPARE(profiles->currentText(), QStringLiteral("bob"));
        QVERIFY(profiles->findText(QStringLiteral("ann")) >= 0);
        QVERIFY(window.selectUser(QStringLiteral("ann")));
        QCOMPARE(settings.user(), QStringLiteral("ann"));

        CreateUserProfileDialog create;
        QCOMPARE(create.windowTitle(), QStringLiteral("Create User Profile"));
        create.nameEdit()->setText(QStringLiteral("  cy "));
        QCOMPARE(create.profileName(), QStringLiteral("cy"));
    }

    // Changing the Project Format (p.1287-1293; 8.7c1b): another format of the same edit rate, kept in the project file;
    // new sequences take it; another edit rate is refused.
    void changingProjectFormat() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("P"));
        project.file = QDir(project.folder).filePath(QStringLiteral("P.cthproj"));
        project.settings.name = QStringLiteral("P");
        auto find = [](const std::string& label) {
            return *std::find_if(projectFormatPresets().begin(), projectFormatPresets().end(),
                                 [&](const ProjectFormat& f) { return f.label() == label; });
        };
        project.settings.format = find("1080p/23.976");
        QDir().mkpath(project.folder);
        QVERIFY(model::ProjectStore::write(project.file, project.settings));
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString bin = window.binContainer().newBin();
        window.binContainer().openBin(bin);
        const QString before = window.binContainer().newSequence(bin);
        QCOMPARE(window.binContainer().document(bin)->bin().item(before)->fields.value(QStringLiteral("Format")),
                 QStringLiteral("1080p/23.976"));
        // The same edit rate: changed, written, and new sequences use it.
        const ProjectFormat uhd = find("UHD 23.976p");
        QVERIFY(window.setProjectFormat(uhd));
        QCOMPARE(window.project()->settings.format, uhd);
        QCOMPARE(model::ProjectStore::read(project.file)->format, uhd);
        const QString after = window.binContainer().newSequence(bin);
        QCOMPARE(window.binContainer().document(bin)->bin().item(after)->fields.value(QStringLiteral("Format")),
                 QStringLiteral("UHD 23.976p"));
        // The sequence made before keeps its format.
        QCOMPARE(window.binContainer().document(bin)->bin().item(before)->fields.value(QStringLiteral("Format")),
                 QStringLiteral("1080p/23.976"));
        // "You can select only projects with the same edit rate" (p.1288).
        QVERIFY(!window.setProjectFormat(find("1080p/25")));
        QCOMPARE(window.project()->settings.format, uhd);
    }

    void mainWindowAppliesSettings() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.show();
        Settings& settings = window.settings();
        settings.setUser(dir.filePath("Users"), QStringLiteral("ed"));

        // File > Settings and Windows > Site Settings need a project.
        QVERIFY(!commands.action("file.settings")->isEnabled());
        const auto project = model::ProjectStore(dir.filePath("Projects")).create([] {
            model::ProjectSettings s;
            s.name = QStringLiteral("Film");
            s.format = projectFormatPresets().front();
            return s;
        }());
        QVERIFY(project);
        window.setProject(*project);
        QVERIFY(QFile::exists(QDir(project->folder).filePath("Settings.json")));
        QVERIFY(commands.action("file.settings")->isEnabled());
        commands.action("file.settings")->trigger();
        auto* sw = window.findChild<SettingsWindow*>();
        QVERIFY(sw && sw->isVisible());
        commands.action("windows.site_settings")->trigger();
        auto* site = window.findChild<SiteSettingsWindow*>();
        QVERIFY(site && site->isVisible());

        // Timeline settings (p.1218-1220).
        auto* toolbar = window.timeline().findChild<QWidget*>(QStringLiteral("timelineTopToolbar"));
        QVERIFY(toolbar->isVisible());
        settings.setValue("Timeline", "showToolbar", false);
        QVERIFY(!toolbar->isVisible());
        QVERIFY(window.timeline().options().markedRegion);
        settings.setValue("Timeline", "showMarkedRegion", false);
        QVERIFY(!window.timeline().options().markedRegion);
        settings.setValue("Timeline", "movementDuringPlay", QStringLiteral("none"));
        QCOMPARE(window.timeline().options().movement, Timeline::Options::Movement::None);
        settings.setValue("Timeline", "onlyOneSegmentTool", true);
        window.timeline().setSegmentTool(model::EditKind::Overwrite, true);
        window.timeline().setSegmentTool(model::EditKind::SpliceIn, true);
        QVERIFY(!window.timeline().isSegmentTool(model::EditKind::Overwrite));
        settings.setValue("Timeline", "defaultSegmentTool", QStringLiteral("insert"));
        QCOMPARE(window.timeline().options().defaultSegmentTool, model::EditKind::SpliceIn);

        // Composer settings (p.1158-1160).
        Composer& composer = window.composer();
        auto* controls = composer.findChild<QWidget*>(QStringLiteral("recordControls"));
        // The button rows; the Play button's playing state lies over them.
        QList<QWidget*> rows = controls->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        rows.removeIf([](QWidget* w) { return w->objectName() == QLatin1String("playState"); });
        QCOMPARE(rows.size(), 2);
        settings.setValue("Composer", "buttonDisplay", QStringLiteral("one"));
        QVERIFY(rows.at(0)->isVisible());
        QVERIFY(!rows.at(1)->isVisible());
        settings.setValue("Composer", "buttonDisplay", QStringLiteral("off"));
        QVERIFY(!controls->isVisible());
        settings.setValue("Composer", "dataDisplay", QStringLiteral("off"));
        QVERIFY(!composer.clipNameButton(MonitorId::Record)->isVisible());
        settings.setValue("Composer", "dataDisplay", QStringLiteral("one"));
        settings.setValue("Composer", "centerDuration", false);
        QVERIFY(composer.clipNameButton(MonitorId::Record)->isVisible());
        QVERIFY(!composer.display().centerDuration);
        settings.setValue("Composer", "showSpannedMarkers", false);
        QVERIFY(!composer.display().spannedMarkers);
        settings.setValue("Composer", "activateRecordAfterEdit", true);
        settings.setValue("Composer", "stopAtTail", true);
        QVERIFY(window.timeline().options().activateRecordAfterEdit);
        QVERIFY(window.timeline().options().stopAtTail);
        settings.setValue("Trim", "autoFocus", true);
        QVERIFY(window.timeline().options().trimAutoFocus);

        // Bin settings (p.1152-1153) and General settings (p.1192).
        settings.setValue("Bin", "favoritesAt", QStringLiteral("bottom"));
        QVERIFY(!FavoriteBins::atTop());
        settings.setValue("Bin", "favoritesAt", QStringLiteral("top"));
        QVERIFY(FavoriteBins::atTop());
        settings.setValue("Bin", "openBin", QStringLiteral("float"));
        QVERIFY(window.binContainer().opensBinsFloating());
        settings.setValue("Bin", "quickFilter", QStringLiteral("all"));
        QVERIFY(BinFilterModel::searchAllColumns());
        settings.setValue("Bin", "quickFilter", QStringLiteral("name"));
        settings.setValue("General", "defaultStartTc", QStringLiteral("10:00:00:00"));
        QCOMPARE(window.binContainer().defaultStartTimecode(), QStringLiteral("10:00:00:00"));

        // Interface settings (p.1200-1201).
        settings.setValue("Interface", "composerColors", true);
        QVERIFY(composer.sourceRecordColors());
        settings.setValue("Interface", "workspaceMove", QStringLiteral("saved"));
        QCOMPARE(window.workspaces().moveBehavior(), WorkspaceManager::MoveBehavior::LastSavedState);
        settings.setValue("Interface", "showToolTips", false);
        QHelpEvent tip(QEvent::ToolTip, QPoint(1, 1), toolbar->mapToGlobal(QPoint(1, 1)));
        // The filter swallows it: QApplication::notify reports it handled.
        QVERIFY(QApplication::sendEvent(composer.clipNameButton(MonitorId::Record), &tip));

        // Edit > Duplicate acts on the active Settings window.
        sw->showTab(SettingsWindow::Tab::User);
        SettingsList* list = sw->list(SettingsScope::User);
        list->selectEntries({settings.file(SettingsScope::User).activeIndex("Bin")});
        sw->activateWindow();
        if (QTest::qWaitForWindowActive(sw) && QApplication::activeWindow() == sw) {
            commands.action("edit.duplicate")->trigger();
            QCOMPARE(settings.file(SettingsScope::User).versions("Bin").size(), 2);
        }

        window.clearProject();
        QVERIFY(!sw->isVisible());
        QVERIFY(!commands.action("file.settings")->isEnabled());
    }

    void moveTabStops() {
        // Composer settings, Move tab (p.1160).
        Composer composer;
        Timeline timeline(composer);
        const model::BinItem s = sequence();
        composer.load(MonitorId::Record, QStringLiteral("bin"), s);
        Timeline::Options o;
        timeline.setOptions(o);
        QCOMPARE(composer.stops(MonitorId::Record), (QList<std::int64_t>{0, 50, 100}));
        o.stopAtTail = true;
        timeline.setOptions(o);
        QCOMPARE(composer.stops(MonitorId::Record), (QList<std::int64_t>{0, 49, 50, 99, 100}));
        o.stopAtHead = false;
        timeline.setOptions(o);
        QCOMPARE(composer.stops(MonitorId::Record), (QList<std::int64_t>{0, 49, 99, 100}));
        // Mark Clip still uses the edits.
        QCOMPARE(composer.events(MonitorId::Record), (QList<std::int64_t>{0, 50, 100}));
    }

    void trimAutoFocus() {
        // Trim settings (p.1223): entering Trim mode zooms in, leaving it
        // zooms back.
        Composer composer;
        Timeline timeline(composer);
        timeline.resize(900, 300);
        timeline.show();
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        composer.moveTo(MonitorId::Record, 45);
        QVERIFY(timeline.enterTrimMode(0));
        QVERIFY(!timeline.isFocused());
        timeline.exitTrimMode();
        Timeline::Options o;
        o.trimAutoFocus = true;
        timeline.setOptions(o);
        QVERIFY(timeline.enterTrimMode(0));
        QVERIFY(timeline.isFocused());
        timeline.exitTrimMode();
        QVERIFY(!timeline.isFocused());
    }
};

QTEST_MAIN(TestSettingsWindow)
#include "tst_settings_window.moc"
