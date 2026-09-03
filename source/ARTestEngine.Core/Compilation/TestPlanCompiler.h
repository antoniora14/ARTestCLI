#pragma once

#include "../Catalog/ComponentCatalog.h"
#include "../Diagnostics.h"
#include "../Model/CompiledStep.h"
#include "../Model/TestPlan.h"

#include <vector>

namespace artest
{
    class TestPlanCompiler final
    {
    public:
        explicit TestPlanCompiler(const ComponentCatalog& catalog) : m_catalog(catalog) {}

        [[nodiscard]] ValueResult<std::vector<CompiledStep>> Compile(const TestPlan& plan) const;

    private:
        const ComponentCatalog& m_catalog;
    };
}
