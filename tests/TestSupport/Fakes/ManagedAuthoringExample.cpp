#include <ARTest/Authoring.h>
#include <cassert>

using namespace artest::sdk;
namespace
{
class Source final : public InstrumentDriver
{
  public:
    Source() { throw std::runtime_error("Metadata must not construct drivers"); }
    Result Initialize(const Parameters &, Context &) override { return Result::Success(); }
    Result Shutdown(Context &) override { return Result::Success(); }
};
class Read final : public Command
{
  public:
    Read() { throw std::runtime_error("Metadata must not construct commands"); }
    Result Execute(const Parameters &, Context &) override { return Result::Success(); }
};
MetadataBundle Example(bool reversed, const std::string &name, const std::string &description)
{
    const IdentityNamespace ids{"com.example.scope"};
    const std::string contract = "com.example.contract.scope.v1";
    Extension extension{ids.Id(), "0.0.1", "Scope", "Example"};
    extension.AddDriver<Source>(ids.Driver("driver", "Scope", contract, DriverMode::Simulated,
        {.schema = Schema::Object(), .description = description}));
    const auto read = [&] { extension.AddCommand<Read>(ids.Command("read", name,
        {.schema = Schema::Object(), .requiredContracts = {contract}})); };
    const auto trigger = [&] { extension.AddCommand<Read>(ids.Command("trigger", "Set_Trigger",
        {.schema = Schema::Object(), .requiredContracts = {contract}})); };
    if (reversed) { trigger(); read(); } else { read(); trigger(); }
    return GenerateMetadata(extension, "Example.dll");
}
}
int main()
{
    const auto original = Example(false, "Read_Wave_Form", "");
    assert(original.manifest == Example(true, "Read_Wave_Form", "").manifest);
    const auto renamed = Example(true, "Read Wave Form", "Captura \"canal\"\n\\ \xc3\xb1");
    assert(original.schemas == renamed.schemas);
    assert(!original.manifest["components"][0].contains("description"));
    assert(renamed.manifest["components"][0].contains("description"));
    for (std::size_t i = 0; i < 3; ++i)
        assert(original.manifest["components"][i]["typeId"] == renamed.manifest["components"][i]["typeId"]);
    for (const auto &invalid : {std::string(513, 'x'), std::string("a\0b", 3), std::string("\xed\xa0\x80")})
    {
        bool rejected = false;
        try { (void)Example(false, "Read", invalid); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
    }
}
