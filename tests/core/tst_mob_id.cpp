#include "lumenis/core/mob_id.h"

#include <QTest>

#include <set>

using lumenis::MobId;

class TestMobId : public QObject {
    Q_OBJECT

private slots:
    void generated() {
        // SMPTE ST 330 basic UMID: label, length 0x13, instance 0, a UUID.
        const MobId id = MobId::generate();
        QVERIFY(!id.isNull());
        const auto& b = id.bytes();
        const std::array<std::uint8_t, 13> head{0x06, 0x0a, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x0f, 0x10, 0x13};
        for (std::size_t i = 0; i < head.size(); ++i) QCOMPARE(b[i], head[i]);
        QCOMPARE(b[13], std::uint8_t{0});
        QCOMPARE(b[14], std::uint8_t{0});
        QCOMPARE(b[15], std::uint8_t{0});
        QCOMPARE(b[22] & 0xf0, 0x40); // UUID version 4
        QCOMPARE(b[24] & 0xc0, 0x80); // RFC 4122 variant
        std::set<MobId> seen;
        for (int i = 0; i < 10000; ++i) QVERIFY(seen.insert(MobId::generate()).second);
    }

    void text() {
        const MobId id = MobId::generate();
        const std::string s = id.toString();
        QCOMPARE(s.size(), std::size_t{15 + 64 + 7});
        QVERIFY(s.starts_with("urn:smpte:umid:060a2b34.01010101.01010f10.13000000."));
        QCOMPARE(MobId::fromString(s), id);
        // What bmx printed for a file FFmpeg wrote (upper case, no prefix).
        const char* bmx = "060a2b34.01010105.01010d00.139d29fb.52947134.c29d29fb.00529471.34c29d00";
        const auto parsed = MobId::fromString(bmx);
        QVERIFY(parsed);
        QCOMPARE(parsed->bytes()[7], std::uint8_t{0x05});
        QCOMPARE(parsed->toString(), "urn:smpte:umid:" + std::string(bmx));
        QCOMPARE(MobId::fromString("URN:SMPTE:UMID:060A2B34010101050101" "0D00139D29FB52947134C29D29FB0052947134C29D00"), parsed);
        QVERIFY(!MobId::fromString(""));
        QVERIFY(!MobId::fromString("urn:smpte:umid:060a2b34"));
        QVERIFY(!MobId::fromString(std::string(64, 'g')));
        QVERIFY(!MobId::fromString(std::string(66, '0')));
        QVERIFY(MobId().isNull());
        QCOMPARE(MobId::fromString(std::string(64, '0')), MobId());
    }
};

QTEST_GUILESS_MAIN(TestMobId)
#include "tst_mob_id.moc"
