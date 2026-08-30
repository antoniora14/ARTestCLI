#include "ArtInstruments/InstrumentFactory.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    class CountingInstrument final : public IInstrument
    {
    public:
        static inline int initializationCount = 0;
        static inline int shutdownCount = 0;

        std::string GetId() const override { return m_id; }
        void SetId(std::string id) override { m_id = std::move(id); }

        OperationResult Initialize(const nlohmann::json& params) override
        {
            ++initializationCount;
            if (params.value("fail", false))
            {
                return OperationResult::Failure("FAKE_INIT_FAILED", "Requested failure.");
            }
            return OperationResult::Success();
        }

        void Shutdown() noexcept override
        {
            ++shutdownCount;
        }

    private:
        std::string m_id;
    };

    void RegisterCountingInstrument()
    {
        static const bool registered = []
        {
            InstrumentFactory::RegisterInstrument("Test.CountingInstrument", []
            {
                return std::make_unique<CountingInstrument>();
            });
            return true;
        }();
        (void)registered;
    }

    nlohmann::json Definition(bool fail = false)
    {
        return nlohmann::json::array({{
            {"type", "Test.CountingInstrument"},
            {"id", "fake-1"},
            {"config", {{"fail", fail}}}
        }});
    }
}

TEST(InstrumentFactoryTests, LoadingDefinitionsDoesNotInitializeHardware)
{
    RegisterCountingInstrument();
    CountingInstrument::initializationCount = 0;
    InstrumentFactory factory;

    const OperationResult result = factory.LoadDefinitions(Definition());

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(CountingInstrument::initializationCount, 0);
    ASSERT_NE(factory.GetInstrument("fake-1"), nullptr);
    EXPECT_EQ(factory.GetInstrument("fake-1")->GetId(), "fake-1");
}

TEST(InstrumentFactoryTests, InitializeAllActivatesDefinitionsExplicitly)
{
    RegisterCountingInstrument();
    CountingInstrument::initializationCount = 0;
    CountingInstrument::shutdownCount = 0;
    InstrumentFactory factory;
    ASSERT_TRUE(factory.LoadDefinitions(Definition()).Succeeded());

    const OperationResult result = factory.InitializeAll();
    factory.ShutdownAll();

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(CountingInstrument::initializationCount, 1);
    EXPECT_EQ(CountingInstrument::shutdownCount, 1);
}

TEST(InstrumentFactoryTests, RejectsUnknownTypesAndDuplicateIdentifiers)
{
    RegisterCountingInstrument();
    InstrumentFactory factory;
    const nlohmann::json definitions = nlohmann::json::array({
        {{"type", "Unknown"}, {"id", "unknown"}, {"config", nlohmann::json::object()}},
        {{"type", "Test.CountingInstrument"}, {"id", "duplicate"}, {"config", nlohmann::json::object()}},
        {{"type", "Test.CountingInstrument"}, {"id", "duplicate"}, {"config", nlohmann::json::object()}}
    });

    const OperationResult result = factory.LoadDefinitions(definitions);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.size(), 2U);
    EXPECT_EQ(factory.GetInstrument("duplicate"), nullptr);
}

TEST(InstrumentFactoryTests, InitializationFailureIsReportedAndCleanedUp)
{
    RegisterCountingInstrument();
    CountingInstrument::initializationCount = 0;
    InstrumentFactory factory;
    ASSERT_TRUE(factory.LoadDefinitions(Definition(true)).Succeeded());

    const OperationResult result = factory.InitializeAll();

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "FAKE_INIT_FAILED");
    EXPECT_EQ(CountingInstrument::initializationCount, 1);
}
