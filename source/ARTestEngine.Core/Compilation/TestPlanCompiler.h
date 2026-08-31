#pragma once

#include "../Commands/CommandRegistry.h"
#include "../Diagnostics.h"
#include "../Instruments/InstrumentManager.h"
#include "../Model/CompiledStep.h"
#include "../Model/TestPlan.h"

#include <vector>

namespace artest
{
    class TestPlanCompiler final
    {
    public:
        TestPlanCompiler(CommandRegistry& commands, InstrumentManager& instruments);

        [[nodiscard]] ValueResult<std::vector<CompiledStep>> Compile(const TestPlan& plan) const;

    private:
        CommandRegistry& m_commands;
        InstrumentManager& m_instruments;
    };
}
