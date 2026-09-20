Set-StrictMode -Version 3.0

$script:GuidedProjectName = 'artest-sdk-project.json'
$script:GuidedLocalName = 'artest-sdk-project.local.json'
$script:GuidedSchema = 'artest.schema.sdk-authoring-project.v1'
$script:SupportedVariants = @('driver-command', 'driver-only', 'command-only')

function Read-GuidedArguments {
    param(
        [string[]]$Values,
        [string[]]$AllowedOptions,
        [string[]]$Switches = @(),
        [int]$MaximumPositionals = 0
    )

    $argumentValues = @($Values | Where-Object { -not [string]::IsNullOrEmpty([string]$_) })
    $options = @{}
    $positionals = [Collections.Generic.List[string]]::new()
    for ($index = 0; $index -lt $argumentValues.Count; $index++) {
        $value = [string]$argumentValues[$index]
        if (-not $value.StartsWith('-')) {
            $positionals.Add($value)
            continue
        }

        $optionText = $value.TrimStart('-')
        $inlineValue = $null
        $equals = $optionText.IndexOf('=')
        if ($equals -ge 0) {
            $inlineValue = $optionText.Substring($equals + 1)
            $optionText = $optionText.Substring(0, $equals)
        }
        $key = $optionText.ToLowerInvariant()
        if ($Switches -contains $key) {
            if ($null -ne $inlineValue) {
                Stop-Kit 'ARTESTSDK001' "--$key does not accept a value."
            }
            $options[$key] = $true
            continue
        }
        if ($AllowedOptions -notcontains $key) {
            Stop-Kit 'ARTESTSDK001' "Unknown option --$optionText."
        }
        if ($options.ContainsKey($key)) {
            Stop-Kit 'ARTESTSDK001' "Option --$key was supplied more than once."
        }
        if ($null -eq $inlineValue) {
            $index++
            if ($index -ge $argumentValues.Count -or ([string]$argumentValues[$index]).StartsWith('--')) {
                Stop-Kit 'ARTESTSDK001' "Option --$key requires a value."
            }
            $inlineValue = [string]$argumentValues[$index]
        }
        if ([string]::IsNullOrWhiteSpace($inlineValue)) {
            Stop-Kit 'ARTESTSDK001' "Option --$key requires a nonempty value."
        }
        $options[$key] = $inlineValue
    }
    if ($positionals.Count -gt $MaximumPositionals) {
        Stop-Kit 'ARTESTSDK001' 'Too many positional arguments.'
    }
    return [pscustomobject]@{ Options = $options; Positionals = @($positionals) }
}

function Read-GuidedValue {
    param(
        [hashtable]$Options,
        [string]$Name,
        [string]$Prompt,
        [string]$Default,
        [switch]$Required
    )
    if ($Options.ContainsKey($Name)) {
        return [string]$Options[$Name]
    }
    try {
        $suffix = if ([string]::IsNullOrEmpty($Default)) { '' } else { " [$Default]" }
        $value = Read-Host "$Prompt$suffix"
    }
    catch {
        if (-not [string]::IsNullOrEmpty($Default)) { return $Default }
        Stop-Kit 'ARTESTSDK001' "Missing --$Name. Supply it explicitly when input is unavailable."
    }
    if ([string]::IsNullOrWhiteSpace($value)) {
        $value = $Default
    }
    if ($Required -and [string]::IsNullOrWhiteSpace($value)) {
        Stop-Kit 'ARTESTSDK001' "Missing --$Name."
    }
    return $value
}

function Get-ProjectSlug {
    param([string]$Name)
    $normalized = $Name.Normalize([Text.NormalizationForm]::FormD)
    $builder = [Text.StringBuilder]::new()
    foreach ($character in $normalized.ToCharArray()) {
        if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($character) -eq
            [Globalization.UnicodeCategory]::NonSpacingMark) {
            continue
        }
        $lower = [char]::ToLowerInvariant($character)
        if (($lower -ge 'a' -and $lower -le 'z') -or ($lower -ge '0' -and $lower -le '9')) {
            $null = $builder.Append($lower)
        }
        elseif ($builder.Length -gt 0 -and $builder[$builder.Length - 1] -ne '-') {
            $null = $builder.Append('-')
        }
    }
    $slug = $builder.ToString().Trim('-')
    if ([string]::IsNullOrWhiteSpace($slug)) { $slug = 'extension' }
    if ($slug[0] -ge '0' -and $slug[0] -le '9') { $slug = "extension-$slug" }
    return $slug
}

function Assert-ProjectName {
    param([string]$Name)
    if ([string]::IsNullOrWhiteSpace($Name) -or $Name -ne $Name.Trim() -or
        $Name.EndsWith('.') -or [IO.Path]::IsPathRooted($Name) -or
        [IO.Path]::GetFileName($Name) -ne $Name -or
        $Name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        Stop-Kit 'ARTESTSDK002' 'Project name must be one valid folder name without leading/trailing whitespace.'
    }
    $reserved = @('con', 'prn', 'aux', 'nul', 'clock$') +
        @(1..9 | ForEach-Object { "com$_" }) + @(1..9 | ForEach-Object { "lpt$_" })
    if ($reserved -contains $Name.Split('.')[0].ToLowerInvariant()) {
        Stop-Kit 'ARTESTSDK002' "Project name is reserved by Windows: $Name"
    }
}

function ConvertTo-PortablePath {
    param([string]$Path)
    return $Path.Replace('\', '/')
}

function Write-JsonFile {
    param([string]$Path, [object]$Value)
    $Value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Write-GuidedProject {
    param(
        [string]$Root,
        [string]$Name,
        [string]$Language,
        [string]$Variant,
        [string]$ExtensionId,
        [AllowNull()][string]$DriverId,
        [AllowNull()][string]$CommandId,
        [string]$ContractId,
        [AllowNull()][string]$ExternalDriverId,
        [string]$Plan,
        [AllowNull()][string]$ProjectFile
    )
    $configuration = [ordered]@{
        schema = $script:GuidedSchema
        schemaVersion = 1
        name = $Name
        language = $Language
        variant = $Variant
        extensionId = $ExtensionId
        driverId = $DriverId
        commandId = $CommandId
        contractId = $ContractId
        externalDriverId = $ExternalDriverId
        plan = $Plan
    }
    if ($ProjectFile) { $configuration.projectFile = $ProjectFile }
    Write-JsonFile (Join-Path $Root $script:GuidedProjectName) $configuration
}

function Write-PythonVariant {
    param(
        [string]$Root,
        [string]$Variant,
        [string]$ExtensionId,
        [AllowNull()][string]$DriverId,
        [AllowNull()][string]$CommandId,
        [string]$ContractId,
        [string]$ExternalDriverId,
        [string]$Author
    )
    if ($Variant -eq 'driver-command') { return }

    $authorLiteral = $Author | ConvertTo-Json -Compress
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('"""Hardware-free ARTest starter generated by the SDK."""')
    $lines.Add('from dataclasses import dataclass')
    $lines.Add('')
    if ($Variant -eq 'driver-only') {
        $lines.Add('from artest_sdk import Driver, Extension, Result, operation')
    }
    else {
        $lines.Add('from artest_sdk import Command, Extension, Result, parameter')
    }
    $lines.Add('')
    $lines.Add('')
    $lines.Add("EXTENSION_ID = `"$ExtensionId`"")
    $lines.Add("CONTRACT = `"$ContractId`"")
    if ($Variant -eq 'command-only') {
        $lines.Add('READ_OPERATION = "com.example.artest.instrument.value-source.v1/read"')
    }
    else {
        $lines.Add('READ_OPERATION = CONTRACT + "/read"')
    }
    $lines.Add('')
    $lines.Add('')
    if ($Variant -eq 'driver-only') {
        $lines.Add('@dataclass')
        $lines.Add('class Configuration:')
        $lines.Add('    value: float = 5.0')
        $lines.Add('')
        $lines.Add('')
        $lines.Add('@dataclass')
        $lines.Add('class ReadParameters:')
        $lines.Add('    channel: int = 1')
        $lines.Add('')
        $lines.Add('')
        $lines.Add('class SimulatedSource(Driver):')
        $lines.Add('    def __init__(self):')
        $lines.Add('        self.value = 0.0')
        $lines.Add('')
        $lines.Add('    async def initialize(self, config, context):')
        $lines.Add('        self.value = config.value')
        $lines.Add('        context.log("GUIDED_DRIVER_INITIALIZE")')
        $lines.Add('        return Result()')
        $lines.Add('')
        $lines.Add('    @operation(READ_OPERATION, ReadParameters)')
        $lines.Add('    async def read(self, parameters, context):')
        $lines.Add('        context.check_cancelled()')
        $lines.Add("        return Result({`"channel`": parameters.channel, `"value`": self.value},")
        $lines.Add("                      `"$ExtensionId.schema.simulated-source.read.v1`")")
        $lines.Add('')
        $lines.Add('    async def shutdown(self, context):')
        $lines.Add('        self.value = 0.0')
        $lines.Add('        context.log("GUIDED_DRIVER_SHUTDOWN")')
        $lines.Add('        return Result()')
        $lines.Add('')
        $lines.Add('')
        $lines.Add('def define_extension():')
        $lines.Add("    extension = Extension(EXTENSION_ID, `"0.1.0`", `"Simulated source`", $authorLiteral)")
        $lines.Add('    extension.driver(')
        $lines.Add("        `"$DriverId`", SimulatedSource, Configuration,")
        $lines.Add('        name="Simulated source", contract=CONTRACT, simulated=True,')
        $lines.Add('    )')
    }
    else {
        $lines.Add('@dataclass')
        $lines.Add('class MeasurementParameters:')
        $lines.Add('    factor: float = parameter(default=1.0, minimum=0.0, maximum=10.0)')
        $lines.Add('')
        $lines.Add('')
        $lines.Add('class MeasureValue(Command):')
        $lines.Add('    async def execute(self, parameters, context):')
        $lines.Add('        async with context.instrument(CONTRACT) as source:')
        $lines.Add('            measurement = await source.invoke(READ_OPERATION, {})')
        $lines.Add('        source_value = measurement.data["value"]')
        $lines.Add('        value = source_value * parameters.factor')
        $lines.Add('        return Result(')
        $lines.Add('            {"value": value, "sourceValue": source_value, "factor": parameters.factor,')
        $lines.Add('             "instrumentId": context.instrument_id},')
        $lines.Add("            `"$ExtensionId.schema.measurement.value.v1`",")
        $lines.Add('            message=f"Computed value {value:.6f}.",')
        $lines.Add('        )')
        $lines.Add('')
        $lines.Add('')
        $lines.Add('def define_extension():')
        $lines.Add("    extension = Extension(EXTENSION_ID, `"0.1.0`", `"Measure a compatible source`", $authorLiteral)")
        $lines.Add('    extension.command(')
        $lines.Add("        `"$CommandId`", MeasureValue, MeasurementParameters,")
        $lines.Add('        name="Measure value", requires=(CONTRACT,),')
        $lines.Add('    )')
    }
    $lines.Add('    return extension')
    Set-Content -LiteralPath (Join-Path $Root 'src\extension.py') -Value $lines -Encoding utf8

    if ($Variant -eq 'driver-only') {
        $plan = [ordered]@{
            format = 'ARTest.Script'; version = 1
            instruments = @([ordered]@{ type = $DriverId; id = 'SimulatedSource1'; config = [ordered]@{ value = 5.0 } })
            commands = @()
        }
    }
    else {
        $plan = [ordered]@{
            format = 'ARTest.Script'; version = 1
            instruments = @([ordered]@{ type = $ExternalDriverId; id = 'CompatibleSource1'; config = [ordered]@{ initialValue = 42 } })
            commands = @([ordered]@{
                stepId = 1; name = $CommandId; instrument = 'CompatibleSource1'
                params = [ordered]@{ factor = 2 }
                policy = [ordered]@{ maxAttempts = 1; timeoutMs = 2000; onFailure = 'stop' }
            })
        }
    }
    Write-JsonFile (Join-Path $Root 'plan\measurement.json') $plan
}

function Write-PythonLocalConfiguration {
    param([string]$Root, [object]$Paths)
    Write-JsonFile (Join-Path $Root 'artest-project.local.json') ([ordered]@{
        schemaVersion = 1
        python = ConvertTo-PortablePath $Paths.privatePython
        sdkWheel = ConvertTo-PortablePath $Paths.pythonSdkWheel
        cliExecutable = ConvertTo-PortablePath $Paths.cliExecutable
        vendorPaths = [ordered]@{}
        vendorDlls = @()
        planBindings = @()
    })
}

function Get-VisualStudioRoot {
    param([string]$MSBuild)
    $current = Split-Path -Parent $MSBuild
    for ($index = 0; $index -lt 3; $index++) { $current = Split-Path -Parent $current }
    return $current
}

function Assert-NativeToolchain {
    param([string]$MSBuild)
    $action = 'Install Visual Studio 18 Insiders with Desktop development with C++ and MSVC v145 x64, then pass --msbuild <path-to-MSBuild.exe>.'
    if ([string]::IsNullOrWhiteSpace($MSBuild)) {
        Stop-Kit 'ARTESTSDK005' "No supported C++ compiler was found. $action"
    }
    try { $MSBuild = [IO.Path]::GetFullPath($MSBuild) }
    catch { Stop-Kit 'ARTESTSDK005' "The MSBuild path is invalid. $action" }
    if (-not (Test-Path -LiteralPath $MSBuild -PathType Leaf)) {
        Stop-Kit 'ARTESTSDK005' "MSBuild is missing at $MSBuild. $action"
    }
    $visualStudio = Get-VisualStudioRoot $MSBuild
    $compiler = @(Get-ChildItem -LiteralPath (Join-Path $visualStudio 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe' } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1)
    $toolset = @(Get-ChildItem -LiteralPath (Join-Path $visualStudio 'MSBuild\Microsoft\VC') -Recurse -File -Filter Toolset.props -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '[\\/]x64[\\/]PlatformToolsets[\\/]v145[\\/]Toolset\.props$' } |
        Select-Object -First 1)
    if ($compiler.Count -ne 1 -or $toolset.Count -ne 1) {
        Stop-Kit 'ARTESTSDK005' "The selected Visual Studio installation lacks the x64 compiler or v145 toolset. $action"
    }
    return $MSBuild
}

function Find-NativeToolchain {
    param([AllowNull()][string]$Requested, [AllowNull()][string]$Saved)
    if ($Requested) { return Assert-NativeToolchain $Requested }
    if ($Saved) {
        try { return Assert-NativeToolchain $Saved }
        catch { }
    }
    $candidates = [Collections.Generic.List[string]]::new()
    $command = Get-Command MSBuild.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($command) { $candidates.Add($command.Source) }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installation = (& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath 2>$null | Select-Object -First 1)
        if ($installation) { $candidates.Add((Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe')) }
    }
    foreach ($drive in [IO.DriveInfo]::GetDrives() | Where-Object DriveType -eq Fixed) {
        $root = Join-Path $drive.RootDirectory.FullName 'Program Files\Microsoft Visual Studio'
        if (Test-Path -LiteralPath $root -PathType Container) {
            foreach ($candidate in Get-ChildItem -Path (Join-Path $root '*\*\MSBuild\Current\Bin\MSBuild.exe') -File -ErrorAction SilentlyContinue) {
                $candidates.Add($candidate.FullName)
            }
        }
    }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        try { return Assert-NativeToolchain $candidate }
        catch { }
    }
    return Assert-NativeToolchain $null
}

function Write-CppLocalConfiguration {
    param([string]$Root, [string]$MSBuild, [string]$NativeSdkRoot)
    Write-JsonFile (Join-Path $Root $script:GuidedLocalName) ([ordered]@{
        schemaVersion = 1
        msbuild = ConvertTo-PortablePath $MSBuild
    })
    $escapedSdk = [Security.SecurityElement]::Escape($NativeSdkRoot)
    @"
<Project>
  <PropertyGroup>
    <ARTestSDKRoot>$escapedSdk</ARTestSDKRoot>
  </PropertyGroup>
</Project>
"@ | Set-Content -LiteralPath (Join-Path $Root 'ARTestSDK.local.props') -Encoding utf8
    $ignorePath = Join-Path $Root '.gitignore'
    $ignore = Get-Content -LiteralPath $ignorePath
    if ($ignore -notcontains $script:GuidedLocalName) {
        Add-Content -LiteralPath $ignorePath -Value $script:GuidedLocalName -Encoding utf8
    }
}

function ConvertTo-CppString {
    param([string]$Value)
    return $Value.Replace('\', '\\').Replace('"', '\"')
}

function Write-CppProject {
    param(
        [string]$Root,
        [string]$Name,
        [string]$Variant,
        [string]$ExtensionId,
        [AllowNull()][string]$DriverId,
        [AllowNull()][string]$CommandId,
        [string]$ContractId,
        [string]$ExternalDriverId,
        [string]$Author,
        [string]$NativeSdkRoot,
        [string]$MSBuild
    )
    $template = Join-Path $NativeSdkRoot 'templates\ARTestExtension'
    if (-not (Test-Path -LiteralPath $template -PathType Container)) {
        Stop-Kit 'ARTESTSDK006' "The native SDK template is missing: $template"
    }
    Copy-Item -LiteralPath $template -Destination $Root -Recurse

    $slug = Get-ProjectSlug $Name
    $projectStem = (($slug.Split('-') | ForEach-Object {
        if ($_.Length -eq 1) { $_.ToUpperInvariant() } else { $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1) }
    }) -join '')
    if ([string]::IsNullOrEmpty($projectStem)) { $projectStem = 'ARTestExtension' }
    $oldProject = Join-Path $Root 'ARTestExtensionStarter.vcxproj'
    $projectFile = "$projectStem.vcxproj"
    $projectPath = Join-Path $Root $projectFile
    $oldProjectPath = [IO.Path]::GetFullPath($oldProject)
    $projectPath = [IO.Path]::GetFullPath($projectPath)
    $reusesTemplatePath = [string]::Equals(
        $oldProjectPath, $projectPath, [StringComparison]::OrdinalIgnoreCase)
    if ($reusesTemplatePath) {
        $projectFile = [IO.Path]::GetFileName($oldProjectPath)
        $projectPath = $oldProjectPath
    }
    $projectText = Get-Content -LiteralPath $oldProject -Raw
    $projectText = $projectText.Replace('ARTestExtensionStarter', $projectStem).
        Replace('{52B2D553-945F-4788-ACD0-6DFAB4EE0C2A}', '{' + [guid]::NewGuid().ToString().ToUpperInvariant() + '}')
    if ($Variant -eq 'driver-only') {
        $projectText = $projectText -replace '(?m)^\s*<ClInclude Include="ReadValueCommand\.h" />\r?\n', ''
        Remove-Item -LiteralPath (Join-Path $Root 'ReadValueCommand.h')
    }
    elseif ($Variant -eq 'command-only') {
        $projectText = $projectText -replace '(?m)^\s*<ClInclude Include="SimulatedValueSource\.h" />\r?\n', ''
        Remove-Item -LiteralPath (Join-Path $Root 'SimulatedValueSource.h')
    }
    if ($Variant -ne 'driver-command') {
        $projectText = $projectText -replace '(?m)^\s*<None Include="MultipleInstruments\.json" />\r?\n', ''
        Remove-Item -LiteralPath (Join-Path $Root 'MultipleInstruments.json')
    }
    Set-Content -LiteralPath $projectPath -Value $projectText -Encoding utf8
    if (-not $reusesTemplatePath) {
        Remove-Item -LiteralPath $oldProject
    }

    if ($DriverId) {
        $driverHeader = Join-Path $Root 'SimulatedValueSource.h'
        $driverText = Get-Content -LiteralPath $driverHeader -Raw
        $driverText = $driverText.Replace('com.example.artest.instrument.value-source.v1/read', "$ContractId/read")
        Set-Content -LiteralPath $driverHeader -Value $driverText -Encoding utf8
    }
    if ($CommandId) {
        $commandHeader = Join-Path $Root 'ReadValueCommand.h'
        $commandText = Get-Content -LiteralPath $commandHeader -Raw
        $commandText = $commandText.Replace('com.example.artest.contract.value-source.v1', $ContractId)
        if ($Variant -ne 'command-only') {
            $commandText = $commandText.Replace(
                'com.example.artest.instrument.value-source.v1/read', "$ContractId/read")
        }
        Set-Content -LiteralPath $commandHeader -Value $commandText -Encoding utf8
    }

    $definition = [Collections.Generic.List[string]]::new()
    if ($CommandId) { $definition.Add('#include "ReadValueCommand.h"') }
    if ($DriverId) { $definition.Add('#include "SimulatedValueSource.h"') }
    $definition.Add('#if defined(ARTEST_METADATA_GENERATOR)')
    $definition.Add('#include <ARTest/MetadataGenerator.h>')
    $definition.Add('#else')
    $definition.Add('#include <ARTest/Extension.h>')
    $definition.Add('#endif')
    $definition.Add('')
    $definition.Add('namespace')
    $definition.Add('{')
    $definition.Add('artest::sdk::Extension DefineExtension()')
    $definition.Add('{')
    $definition.Add('    using artest::sdk::Schema;')
    $definition.Add('')
    $definition.Add(('    artest::sdk::Extension extension{{"{0}", "0.1.0", "{1}", "{2}"}};' -f
        $ExtensionId, (ConvertTo-CppString "$Name extension"), (ConvertTo-CppString $Author)))
    if ($CommandId) {
        $definition.Add('')
        $definition.Add('    extension.AddCommand<artest_extension::ReadValueCommand>(')
        $definition.Add('    {')
        $definition.Add("        .id = `"$CommandId`",")
        $definition.Add('        .name = "Read value",')
        $definition.Add('        .metadata = {')
        $definition.Add('            .schema = Schema::Object().Optional("factor", Schema::Number().Minimum(0).Maximum(10)),')
        $definition.Add("            .schemaId = `"$ExtensionId.parameters.v1`",")
        $definition.Add("            .requiredContracts = {`"$ContractId`"}")
        $definition.Add('        }')
        $definition.Add('    });')
    }
    if ($DriverId) {
        $definition.Add('')
        $definition.Add('    extension.AddDriver<artest_extension::SimulatedValueSource>(')
        $definition.Add('    {')
        $definition.Add("        .id = `"$DriverId`",")
        $definition.Add('        .name = "Simulated value source",')
        $definition.Add("        .contract = `"$ContractId`",")
        $definition.Add('        .mode = artest::sdk::DriverMode::Simulated,')
        $definition.Add('        .metadata = {')
        $definition.Add('            .schema = Schema::Object().Optional("initialValue", Schema::Number().Minimum(-1000000).Maximum(1000000)),')
        $definition.Add("            .schemaId = `"$ExtensionId.configuration.v1`"")
        $definition.Add('        }')
        $definition.Add('    });')
    }
    $definition.Add('')
    $definition.Add('    return extension;')
    $definition.Add('}')
    $definition.Add('} // namespace')
    $definition.Add('')
    $definition.Add('#if defined(ARTEST_METADATA_GENERATOR)')
    $definition.Add('ARTEST_GENERATE_METADATA(DefineExtension)')
    $definition.Add('#else')
    $definition.Add('ARTEST_EXPORT_EXTENSION(DefineExtension)')
    $definition.Add('#endif')
    Set-Content -LiteralPath (Join-Path $Root 'Extension.cpp') -Value $definition -Encoding utf8

    if ($Variant -eq 'driver-only') {
        $plan = [ordered]@{
            format = 'ARTest.Script'; version = 1
            instruments = @([ordered]@{ id = 'ValueSource1'; type = $DriverId; config = [ordered]@{ initialValue = 42 } })
            commands = @()
        }
    }
    elseif ($Variant -eq 'command-only') {
        $plan = [ordered]@{
            format = 'ARTest.Script'; version = 1
            instruments = @([ordered]@{ id = 'CompatibleSource1'; type = $ExternalDriverId; config = [ordered]@{ initialValue = 42 } })
            commands = @([ordered]@{ stepId = 1; name = $CommandId; instrument = 'CompatibleSource1'; params = [ordered]@{ factor = 2 } })
        }
    }
    else {
        $plan = [ordered]@{
            format = 'ARTest.Script'; version = 1
            instruments = @([ordered]@{ id = 'ValueSource1'; type = $DriverId; config = [ordered]@{ initialValue = 42 } })
            commands = @([ordered]@{ stepId = 1; name = $CommandId; instrument = 'ValueSource1'; params = [ordered]@{ factor = 2 } })
        }
        $multiple = Join-Path $Root 'MultipleInstruments.json'
        $multipleText = Get-Content -LiteralPath $multiple -Raw
        $multipleText = $multipleText.Replace('com.example.artest.driver.sim-value-source', $DriverId).
            Replace('com.example.artest.command.read-value', $CommandId)
        Set-Content -LiteralPath $multiple -Value $multipleText -Encoding utf8
    }
    Write-JsonFile (Join-Path $Root 'TestPlan.json') $plan
    Write-CppLocalConfiguration -Root $Root -MSBuild $MSBuild -NativeSdkRoot $NativeSdkRoot
    return $projectFile
}

function Invoke-LowLevelPythonProject {
    param([object]$Paths, [string[]]$ProjectArguments)
    $output = & $Paths.privatePython -I -B $Paths.pythonProjectTool @ProjectArguments 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Stop-Kit 'ARTESTSDK007' "Python project tooling failed ($code): $($output | Out-String)"
    }
    return @($output)
}

function Invoke-GuidedNew {
    param([object]$Paths, [string[]]$Values)
    $parsed = Read-GuidedArguments -Values $Values -AllowedOptions @('name', 'folder', 'language', 'variant', 'author', 'msbuild')
    $name = Read-GuidedValue $parsed.Options 'name' 'Project name' $null -Required
    $folderText = Read-GuidedValue $parsed.Options 'folder' 'Parent folder' $null -Required
    $languageText = Read-GuidedValue $parsed.Options 'language' 'Language (python/cpp)' 'python' -Required
    $suppliedValues = @($Values | Where-Object { -not [string]::IsNullOrEmpty([string]$_) })
    $variant = if ($parsed.Options.ContainsKey('variant')) {
        [string]$parsed.Options.variant
    }
    elseif ($suppliedValues.Count -eq 0) {
        Read-GuidedValue $parsed.Options 'variant' 'Starter (driver-command/driver-only/command-only)' 'driver-command' -Required
    }
    else {
        'driver-command'
    }
    $author = if ($parsed.Options.ContainsKey('author')) { [string]$parsed.Options.author } else { 'ARTest Developer' }

    Assert-ProjectName $name
    $language = $languageText.Trim().ToLowerInvariant()
    if ($language -in @('c#', 'csharp', 'cs', 'dotnet', '.net')) {
        Stop-Kit 'ARTESTSDK003' 'C# is unavailable until the D4.3/.NET gates are completed; choose python or cpp.'
    }
    if ($language -in @('c++', 'cplusplus')) { $language = 'cpp' }
    if ($language -notin @('python', 'cpp')) {
        Stop-Kit 'ARTESTSDK003' "Unsupported language '$languageText'; choose python or cpp."
    }
    $variant = $variant.Trim().ToLowerInvariant().Replace('+', '-').Replace('_', '-')
    if ($variant -eq 'driver-command-command') { $variant = 'driver-command' }
    if ($script:SupportedVariants -notcontains $variant) {
        Stop-Kit 'ARTESTSDK004' "Unsupported starter '$variant'; choose driver-command, driver-only, or command-only."
    }
    if ([string]::IsNullOrWhiteSpace($author) -or $author -match '[\x00-\x1f]') {
        Stop-Kit 'ARTESTSDK001' '--author must be nonempty and cannot contain control characters.'
    }

    try { $folder = [IO.Path]::GetFullPath($folderText) }
    catch { Stop-Kit 'ARTESTSDK002' "Parent folder is invalid: $folderText" }
    if (-not (Test-Path -LiteralPath $folder -PathType Container)) {
        Stop-Kit 'ARTESTSDK002' "Parent folder does not exist: $folder"
    }
    if (((Get-Item -LiteralPath $folder -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        Stop-Kit 'ARTESTSDK002' "Parent folder cannot be a reparse point: $folder"
    }
    $destination = [IO.Path]::GetFullPath((Join-Path $folder $name))
    $prefix = $folder.TrimEnd('\') + '\'
    if (-not $destination.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        Stop-Kit 'ARTESTSDK002' 'Project destination escapes its parent folder.'
    }
    if (Test-Path -LiteralPath $destination) {
        Stop-Kit 'ARTESTSDK002' "Refusing to replace an existing destination: $destination"
    }

    $identity = [guid]::NewGuid().ToString('N').Substring(0, 16)
    # Keep metadata filenames short enough for the existing MSBuild publication
    # transaction while retaining 64 bits of project-local uniqueness.
    $extensionId = "local.$identity"
    $driverId = if ($variant -eq 'command-only') { $null } else { "$extensionId.driver.simulated-source" }
    $commandId = if ($variant -eq 'driver-only') { $null } else { "$extensionId.command.measure-value" }
    $contractId = if ($variant -eq 'command-only') {
        'com.example.artest.contract.value-source.v1'
    } else {
        "$extensionId.contract.simulated-source.v1"
    }
    $externalDriverId = if ($variant -eq 'command-only') { 'com.example.artest.driver.sim-value-source' } else { $null }
    $scratch = Join-Path $folder ('.artest-new-' + [guid]::NewGuid().ToString('N'))
    $msbuild = $null
    try {
        if ($language -eq 'cpp') {
            $requested = if ($parsed.Options.ContainsKey('msbuild')) { [string]$parsed.Options.msbuild } else { $null }
            $msbuild = Find-NativeToolchain -Requested $requested -Saved $null
            $projectFile = Write-CppProject -Root $scratch -Name $name -Variant $variant `
                -ExtensionId $extensionId -DriverId $driverId -CommandId $commandId `
                -ContractId $contractId -ExternalDriverId $externalDriverId -Author $author `
                -NativeSdkRoot $Paths.nativeSdkRoot -MSBuild $msbuild
            Write-GuidedProject -Root $scratch -Name $name -Language $language -Variant $variant `
                -ExtensionId $extensionId -DriverId $driverId -CommandId $commandId `
                -ContractId $contractId -ExternalDriverId $externalDriverId -Plan 'TestPlan.json' `
                -ProjectFile $projectFile
        }
        else {
            $null = Invoke-LowLevelPythonProject $Paths @(
                'create', $scratch, '--extension-id', $extensionId,
                '--driver-id', "$extensionId.driver.simulated-source",
                '--command-id', "$extensionId.command.measure-value", '--author', $author)
            Write-PythonLocalConfiguration -Root $scratch -Paths $Paths
            Write-PythonVariant -Root $scratch -Variant $variant -ExtensionId $extensionId `
                -DriverId $driverId -CommandId $commandId -ContractId $contractId `
                -ExternalDriverId $externalDriverId -Author $author
            Write-GuidedProject -Root $scratch -Name $name -Language $language -Variant $variant `
                -ExtensionId $extensionId -DriverId $driverId -CommandId $commandId `
                -ContractId $contractId -ExternalDriverId $externalDriverId `
                -Plan 'plan/measurement.json' -ProjectFile $null
        }
        Move-Item -LiteralPath $scratch -Destination $destination
    }
    finally {
        if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
    }
    [ordered]@{
        status = 'created'; message = "Created $language $variant project '$name'."
        project = $destination; language = $language; variant = $variant
        extensionId = $extensionId; edit = if ($language -eq 'python') { 'src/extension.py' } else { 'Extension.cpp' }
        next = "& '$kitRoot\artest.ps1' build --project '$destination'"
    } | ConvertTo-Json -Depth 5
}

function Read-GuidedProject {
    param([string]$Root)
    try { $Root = [IO.Path]::GetFullPath($Root) }
    catch { Stop-Kit 'ARTESTSDK008' "Project path is invalid: $Root" }
    $path = Join-Path $Root $script:GuidedProjectName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Stop-Kit 'ARTESTSDK008' "Guided project configuration is missing: $path"
    }
    try { $value = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json }
    catch { Stop-Kit 'ARTESTSDK008' "Cannot read guided project configuration: $($_.Exception.Message)" }
    $required = @('schema', 'schemaVersion', 'name', 'language', 'variant', 'extensionId', 'contractId', 'plan')
    foreach ($name in $required) {
        if ($null -eq $value.PSObject.Properties[$name]) {
            Stop-Kit 'ARTESTSDK008' "Guided project configuration is missing '$name': $path"
        }
    }
    if ($value.schema -ne $script:GuidedSchema -or $value.schemaVersion -ne 1 -or
        $value.language -notin @('python', 'cpp') -or $script:SupportedVariants -notcontains $value.variant) {
        Stop-Kit 'ARTESTSDK008' "Guided project configuration is incompatible: $path"
    }
    return [pscustomobject]@{ Root = $Root; Path = $path; Value = $value }
}

function Read-CppLocalConfiguration {
    param([string]$Root)
    $path = Join-Path $Root $script:GuidedLocalName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    try { $value = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json }
    catch { Stop-Kit 'ARTESTSDK008' "Cannot read C++ local configuration: $($_.Exception.Message)" }
    if ($value.schemaVersion -ne 1 -or [string]::IsNullOrWhiteSpace([string]$value.msbuild)) {
        Stop-Kit 'ARTESTSDK008' "C++ local configuration is incompatible: $path"
    }
    return [string]$value.msbuild
}

function Invoke-GuidedBuild {
    param([object]$Paths, [string[]]$Values)
    $parsed = Read-GuidedArguments -Values $Values -AllowedOptions @('project', 'configuration', 'msbuild') -MaximumPositionals 1
    if ($parsed.Options.ContainsKey('project') -and $parsed.Positionals.Count -gt 0) {
        Stop-Kit 'ARTESTSDK001' 'Specify the project either positionally or with --project, not both.'
    }
    $projectText = if ($parsed.Options.ContainsKey('project')) { [string]$parsed.Options.project }
        elseif ($parsed.Positionals.Count -eq 1) { [string]$parsed.Positionals[0] }
        elseif (Test-Path -LiteralPath (Join-Path (Get-Location) $script:GuidedProjectName) -PathType Leaf) { [string](Get-Location) }
        else { Read-GuidedValue @{} 'project' 'Project folder' $null -Required }
    $project = Read-GuidedProject $projectText

    if ($project.Value.language -eq 'python') {
        if ($parsed.Options.ContainsKey('configuration') -or $parsed.Options.ContainsKey('msbuild')) {
            Stop-Kit 'ARTESTSDK001' '--configuration and --msbuild apply only to C++ projects.'
        }
        $checkOutput = Invoke-LowLevelPythonProject $Paths @('check', $project.Root)
        $check = ($checkOutput | Out-String) | ConvertFrom-Json
        if ($check.success -ne $true) {
            Stop-Kit 'ARTESTSDK007' "Python prerequisite check failed: $($checkOutput | Out-String)"
        }
        $prepareOutput = Invoke-LowLevelPythonProject $Paths @('prepare', $project.Root)
        $prepared = ($prepareOutput | Out-String) | ConvertFrom-Json
        [ordered]@{
            status = 'built'; message = "Prepared Python project '$($project.Value.name)'."
            project = $project.Root; language = 'python'; variant = $project.Value.variant
            extensionId = $project.Value.extensionId; preparationId = $prepared.preparationId
            reused = $prepared.reused; package = $prepared.package; receipt = $prepared.receipt
        } | ConvertTo-Json -Depth 8
        return
    }

    $configuration = if ($parsed.Options.ContainsKey('configuration')) { [string]$parsed.Options.configuration } else { 'Release' }
    if ($configuration -notin @('Debug', 'Release')) {
        Stop-Kit 'ARTESTSDK001' "Unsupported C++ configuration '$configuration'; choose Debug or Release."
    }
    $saved = Read-CppLocalConfiguration $project.Root
    $requested = if ($parsed.Options.ContainsKey('msbuild')) { [string]$parsed.Options.msbuild } else { $null }
    $msbuild = Find-NativeToolchain -Requested $requested -Saved $saved
    Write-CppLocalConfiguration -Root $project.Root -MSBuild $msbuild -NativeSdkRoot $Paths.nativeSdkRoot
    $projectFile = [string]$project.Value.projectFile
    if ([string]::IsNullOrWhiteSpace($projectFile) -or [IO.Path]::IsPathRooted($projectFile) -or
        $projectFile.Replace('\', '/').Split('/') -contains '..') {
        Stop-Kit 'ARTESTSDK008' 'C++ guided project has an unsafe projectFile.'
    }
    $vcxproj = [IO.Path]::GetFullPath((Join-Path $project.Root $projectFile))
    if (-not $vcxproj.StartsWith($project.Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $vcxproj -PathType Leaf)) {
        Stop-Kit 'ARTESTSDK008' "C++ project file is missing or outside the project: $projectFile"
    }
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $msbuild
    $startInfo.UseShellExecute = $false
    foreach ($argument in @($vcxproj, '/m', "/p:Configuration=$configuration", '/p:Platform=x64', '/verbosity:minimal')) {
        $null = $startInfo.ArgumentList.Add($argument)
    }
    # Windows accepts case-distinct environment keys, but MSBuild's native tool
    # task does not. Forward one deterministic value for each logical key.
    $environment = [Environment]::GetEnvironmentVariables()
    $forwarded = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($key in $environment.Keys) {
        $name = [string]$key
        if (-not $forwarded.ContainsKey($name) -or $name -ceq $name.ToUpperInvariant()) {
            $forwarded[$name] = $environment[$key]
        }
    }
    $startInfo.Environment.Clear()
    foreach ($entry in $forwarded.GetEnumerator()) {
        $startInfo.Environment[$entry.Key] = [string]$entry.Value
    }
    $process = [Diagnostics.Process]::Start($startInfo)
    $process.WaitForExit()
    $buildExit = $process.ExitCode
    $process.Dispose()
    if ($buildExit -ne 0) {
        Stop-Kit 'ARTESTSDK009' "C++ build failed with exit code $buildExit. Review the MSBuild diagnostics above."
    }
    $target = [IO.Path]::GetFileNameWithoutExtension($projectFile)
    $binary = Join-Path $project.Root "bin\x64\$configuration\$target.dll"
    $package = Join-Path $project.Root "out\extensions\x64\$configuration\$target"
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $package 'artest-extension.json') -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $package '.artest-generated-package.json') -PathType Leaf)) {
        Stop-Kit 'ARTESTSDK009' 'C++ build completed but did not publish the expected validated package.'
    }
    [ordered]@{
        status = 'built'; message = "Built C++ project '$($project.Value.name)' ($configuration/x64)."
        project = $project.Root; language = 'cpp'; variant = $project.Value.variant
        extensionId = $project.Value.extensionId; configuration = $configuration
        binary = $binary; package = $package; msbuild = $msbuild
    } | ConvertTo-Json -Depth 6
}
