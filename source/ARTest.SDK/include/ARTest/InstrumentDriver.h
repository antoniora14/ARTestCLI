#pragma once

#include "Context.h"
#include "Parameters.h"
#include <functional>
#include <map>

namespace artest::sdk
{

class InstrumentDriver
{
  public:
    virtual ~InstrumentDriver() = default;
    InstrumentDriver() = default;
    InstrumentDriver(const InstrumentDriver &) = delete;
    InstrumentDriver &operator=(const InstrumentDriver &) = delete;

    [[nodiscard]] virtual Result Initialize(const Parameters &configuration, Context &context) = 0;
    [[nodiscard]] virtual Result Shutdown(Context &context) = 0;
    [[nodiscard]] Result Dispatch(std::string_view operation, const Parameters &parameters, Context &context)
    {
        const auto found = m_operations.find(operation);
        if (found == m_operations.end())
            return Result::Failure(Status::OperationNotSupported,
                                   "Unknown driver operation: " + std::string{operation});
        return found->second(parameters, context);
    }

  protected:
    using Handler = std::function<Result(const Parameters &, Context &)>;

    // Register local handlers in the constructor; never acquire hardware there.
    void RegisterOperation(std::string operation, Handler handler)
    {
        if (operation.empty() || operation.find('\0') != std::string::npos || !handler ||
            operation.starts_with("artest.lifecycle.") ||
            operation.starts_with("artest.component.") || operation.starts_with("artest.command."))
            throw std::invalid_argument("A non-reserved operation ID and handler are required.");
        if (!m_operations.emplace(std::move(operation), std::move(handler)).second)
            throw std::invalid_argument("Duplicate driver operation.");
    }

  private:
    std::map<std::string, Handler, std::less<>> m_operations;
};

} // namespace artest::sdk
