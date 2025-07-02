#pragma once
#include "../../ArtCore/ICommands.h"

#define CMD_WAITS_NAME       "Time.WaitUs"
#define CMD_WAITMS_NAME      "Time.WaitMs"
#define CMD_WAITUS_NAME      "Time.WaitUs"


class WaitCommand : public ICommand
{
private:
    int             m_nMilliseconds;

public:
    WaitCommand() = default;
    WaitCommand(int ms) : m_nMilliseconds(ms) {}

    virtual std::string Name() const override;
    virtual bool Validate(std::string& csError) override;
    virtual void Execute(ExecutionContext& context) override;
    void Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument) override;
};

