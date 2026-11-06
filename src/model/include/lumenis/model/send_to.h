// Send To templates (Phase 7.1c2; docs/spec/send-to.md; Editing Guide p.863-867): an export with its Export setting
// values, its destination folder and what happens after it (Auto Launch, Auto Load, Reveal file), kept as a .stt file
// (ours: JSON) and listed as a File > Output > Send To command. The Pro Tools template is built in; its options
// (p.863-864) become one or two exports.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace lumenis::model {

struct SendToTemplate {
    QString name;              // the Send To command: the file's base name
    QJsonObject exportValues;  // Export setting option -> value, as settings.json names them
    QString folder;            // the destination, "the destination folder that you set last" (p.865)
    bool autoLaunch = false;   // Auto Launch the application below after exporting (p.865)
    QString application;       // its path
    bool autoLoad = true;      // Auto Load Exported File(s) in it (p.866)
    bool reveal = false;       // Reveal file: show the exported file in the file manager (p.866)

    QJsonObject toJson() const;
    // Nothing when it has no name.
    static std::optional<SendToTemplate> fromJson(const QJsonObject& o);
};

// The ".stt" templates in `folder`, by name; files that do not read are left out.
QList<SendToTemplate> loadSendToTemplates(const QString& folder);
// Writes the template to `path` (made a ".stt"). False with the reason in `error`.
bool saveSendToTemplate(const QString& path, const SendToTemplate& t, QString* error = nullptr);

// The Pro Tools template's options (p.863-864), in the guide's order. Embedding audio in the file waits for AAF's
// embedded media (`available` false).
struct ProToolsOption {
    QString id;
    QString label;
    bool available = true;
};
const QList<ProToolsOption>& proToolsOptions();
// What a Pro Tools option exports: one or two sets of Export values laid over `base` (the Export setting's), each with
// its own Export As: an AAF of the sequence and, for the QuickTime options, a movie of its picture beside it.
QList<QJsonObject> proToolsExports(const QString& option, const QJsonObject& base);

} // namespace lumenis::model
