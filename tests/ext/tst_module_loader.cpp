#include "lumenis/ext/codec.h"
#include "lumenis/ext/module_loader.h"

#include <QTest>

using namespace lumenis::ext;

class TestModuleLoader : public QObject {
    Q_OBJECT

private slots:
    void loadsModuleAndCastsAcrossTheBoundary() {
        ProviderRegistry registry;
        ModuleLoader loader(registry);
        const ModuleRecord record = loader.load(TEST_MODULE_GOOD);
        QVERIFY2(record.loaded, record.error.c_str());
        QCOMPARE(record.id, std::string("org.lumenis.test.module"));
        QCOMPARE(record.license, std::string("GPL-3.0-or-later"));
        QCOMPARE(record.providersAdded, std::size_t{1});

        // The provider object lives in the module; the cast and the virtual
        // calls exercise RTTI and vtables across the library boundary.
        const auto codecs = registry.usable<CodecProvider>();
        QCOMPARE(codecs.size(), std::size_t{1});
        CodecQuery query;
        query.codec = "test-codec";
        QVERIFY(codecs.front()->canDecode(query));
        QVERIFY(codecs.front()->info().tier == LicenseTier::ThirdParty);
    }

    void refusesSecondCopyOfSameModule() {
        ProviderRegistry registry;
        ModuleLoader loader(registry);
        QVERIFY(loader.load(TEST_MODULE_GOOD).loaded);
        const ModuleRecord again = loader.load(TEST_MODULE_GOOD);
        QVERIFY(!again.loaded);
        QVERIFY(again.error.find("already loaded") != std::string::npos);
        QCOMPARE(registry.all().size(), std::size_t{1});
    }

    void refusesWrongAbi() {
        ProviderRegistry registry;
        ModuleLoader loader(registry);
        const ModuleRecord record = loader.load(TEST_MODULE_BAD_ABI);
        QVERIFY(!record.loaded);
        QVERIFY2(record.error.find("ABI version") != std::string::npos, record.error.c_str());
        QVERIFY(registry.all().empty());
    }

    void rollsBackFailedRegistration() {
        // The module adds a provider, then returns an error: nothing of it may stay registered,
        // so a good module with the same provider id still loads afterwards.
        ProviderRegistry registry;
        ModuleLoader loader(registry);
        const ModuleRecord failed = loader.load(TEST_MODULE_FAILS);
        QVERIFY(!failed.loaded);
        QVERIFY2(failed.error.find("registration failed") != std::string::npos, failed.error.c_str());
        QCOMPARE(failed.providersAdded, std::size_t{0});
        QVERIFY(registry.all().empty());
        QVERIFY(!registry.find("org.lumenis.test.codec"));
        const ModuleRecord good = loader.load(TEST_MODULE_GOOD);
        QVERIFY2(good.loaded, good.error.c_str());
        QCOMPARE(registry.all().size(), std::size_t{1});
    }

    void reportsMissingFile() {
        ProviderRegistry registry;
        ModuleLoader loader(registry);
        const ModuleRecord record = loader.load("does-not-exist-module");
        QVERIFY(!record.loaded);
        QVERIFY(!record.error.empty());
        QCOMPARE(loader.records().size(), std::size_t{1});
    }

    void providersOutliveTheLoader() {
        ProviderRegistry registry;
        {
            ModuleLoader loader(registry);
            QVERIFY(loader.load(TEST_MODULE_GOOD).loaded);
            QVERIFY(!loader.load(TEST_MODULE_GOOD).loaded); // a rejected duplicate
        }
        const auto codecs = registry.usable<CodecProvider>();
        QCOMPARE(codecs.size(), std::size_t{1});
        CodecQuery query;
        query.codec = "test-codec";
        QVERIFY(codecs.front()->canDecode(query));
    }

    void scansDirectories() {
        ProviderRegistry registry;
        ModuleLoader loader(registry);
        const auto records = loader.loadDirectories({TEST_MODULE_GOOD_DIR, "no/such/dir"});
        QCOMPARE(records.size(), std::size_t{1});
        QVERIFY(records.front().loaded);
    }
};

QTEST_GUILESS_MAIN(TestModuleLoader)
#include "tst_module_loader.moc"
