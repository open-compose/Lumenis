// The FCP 7 XML fuzz target (fuzz_targets.h): fcpxml::importXml on arbitrary bytes, as File > Input
// > Import Media reads an FCP XML file (docs/spec/fcp-xml.md); then every sequence it makes is written
// back (fcpxml::exportXml, Export to File's FCP XML, its clips found among the items imported),
// and what is written must import again.
#include "fuzz_targets.h"

#include "lumenis/interop/fcp_xml.h"
#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QTimeZone>

namespace lumenis::fuzz {

namespace fcpxml = interop::fcpxml;
namespace otio = interop::otio;

int fuzzFcpXml(const std::uint8_t* data, std::size_t size) {
    otio::ImportOptions options;
    options.binId = QStringLiteral("fuzz");
    options.when = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::utc()); // the same every run
    const auto imported = fcpxml::importXml(view(data, size), options);
    if (!imported) return 0;
    const QList<model::BinItem>& items = imported->items;
    otio::ExportOptions exportOptions;
    exportOptions.resolve = [&items](const MobId& mob, const QString& clipId) -> std::optional<model::BinItem> {
        for (const model::BinItem& i : items) {
            if (i.kind != model::BinItemKind::Sequence && (i.id == clipId || model::mobIdOf(i) == mob)) return i;
        }
        return std::nullopt;
    };
    for (const model::BinItem& item : items) {
        if (item.kind != model::BinItemKind::Sequence) continue;
        const otio::ExportResult out = fcpxml::exportXml(item, exportOptions);
        fuzzCheck(out.ok, "fcpxml", QStringLiteral("a sequence the import made did not export: %1").arg(out.error));
        QString error;
        fuzzCheck(fcpxml::importXml(out.json, options, &error).has_value(), "fcpxml",
                  QStringLiteral("what the export wrote did not import again: %1").arg(error));
    }
    return 0;
}

} // namespace lumenis::fuzz
