#include "lumenis/ext/codec.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ext/transcription.h"

#include <QTest>

using namespace lumenis::ext;

namespace {

class FakeCodec final : public CodecProvider {
public:
    FakeCodec(std::string id, int priority, bool available = true)
        : id_(std::move(id)), priority_(priority), available_(available) {}

    ProviderInfo info() const override {
        ProviderInfo i;
        i.id = id_;
        i.category = Category::VideoCodec;
        return i;
    }
    Availability probe() const override {
        return available_ ? Availability{} : Availability{false, "missing"};
    }
    int priority() const override { return priority_; }
    bool canDecode(const CodecQuery&) const override { return true; }
    bool canEncode(const CodecQuery&) const override { return false; }
    std::unique_ptr<Decoder> createDecoder(const CodecQuery&) override { return nullptr; }
    std::unique_ptr<Encoder> createEncoder(const CodecQuery&) override { return nullptr; }

private:
    std::string id_;
    int priority_;
    bool available_;
};

QStringList ids(const std::vector<std::shared_ptr<CodecProvider>>& providers) {
    QStringList result;
    for (const auto& p : providers) {
        result << QString::fromStdString(p->info().id);
    }
    return result;
}

} // namespace

class TestProviderRegistry : public QObject {
    Q_OBJECT

private slots:
    void rejectsDuplicateIds() {
        ProviderRegistry registry;
        QVERIFY(registry.add(std::make_shared<FakeCodec>("a", 0)));
        QVERIFY(!registry.add(std::make_shared<FakeCodec>("a", 5)));
        QVERIFY(!registry.add(nullptr));
        QCOMPARE(registry.all().size(), std::size_t{1});
    }

    void ranksByPriorityThenRegistrationOrder() {
        ProviderRegistry registry;
        registry.add(std::make_shared<FakeCodec>("ffmpeg", 0));
        registry.add(std::make_shared<FakeCodec>("os", 10));
        registry.add(std::make_shared<FakeCodec>("other", 0));
        QCOMPARE(ids(registry.usable<CodecProvider>()), (QStringList{"os", "ffmpeg", "other"}));
    }

    void skipsUnavailableAndDisabled() {
        ProviderRegistry registry;
        registry.add(std::make_shared<FakeCodec>("os", 10, false));
        registry.add(std::make_shared<FakeCodec>("ffmpeg", 0));
        registry.add(std::make_shared<FakeCodec>("vendor", 5));
        QCOMPARE(ids(registry.usable<CodecProvider>()), (QStringList{"vendor", "ffmpeg"}));

        registry.setEnabled("vendor", false);
        QVERIFY(!registry.isEnabled("vendor"));
        QCOMPARE(ids(registry.usable<CodecProvider>()), (QStringList{"ffmpeg"}));

        registry.setEnabled("vendor", true);
        QCOMPARE(ids(registry.usable<CodecProvider>()), (QStringList{"vendor", "ffmpeg"}));
    }

    void filtersByInterface() {
        ProviderRegistry registry;
        registry.add(std::make_shared<FakeCodec>("codec", 0));
        QCOMPARE(registry.usable<CodecProvider>().size(), std::size_t{1});
        QVERIFY(registry.usable<TranscriptionProvider>().empty());
        QVERIFY(registry.find("codec"));
        QVERIFY(!registry.find("nope"));
    }
};

QTEST_GUILESS_MAIN(TestProviderRegistry)
#include "tst_provider_registry.moc"
