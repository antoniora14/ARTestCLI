#include "ArtWaitCommand.h"
#include "../../ArtCore/RegisterCommand.h"
#include <thread>
#include <chrono>
#include <iostream>


REGISTER_COMMAND(CMD_WAITMS_NAME, WaitCommand)


std::string WaitCommand::Name() const
{
    return CMD_WAITMS_NAME;
}

bool WaitCommand::Validate(std::string& csError) const
{
    if (m_nMilliseconds < 0)
    {
        csError = "Invalid Wait value, should be greater than zero.";
        return false;
    }
    return true;
}

StepResult WaitCommand::Execute(ExecutionContext& /*context*/)
{
    std::cout << "[Wait] " << m_nMilliseconds << " ms \n";
    std::this_thread::sleep_for(std::chrono::milliseconds(m_nMilliseconds));
    return StepResult::Pass();
}

void WaitCommand::Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument)
{
    (void)spInstrument;
    m_nMilliseconds = params.value("milliseconds", -1);
}
