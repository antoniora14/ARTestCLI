#pragma once

#include <ARTest/InstrumentDriver.h>

namespace artest::extensions
{
    class SimCanDriver final : public sdk::InstrumentDriver
    {
    private:
        sdk::Result Send(const sdk::Parameters& parameters);
        bool m_failShutdown = false;
        std::size_t m_messageCount = 0;

    public:
        SimCanDriver();
        sdk::Result Initialize(const sdk::Parameters &configuration, sdk::Context &context) override;
        sdk::Result Shutdown(sdk::Context &context) override;
};

} // namespace artest::extensions
