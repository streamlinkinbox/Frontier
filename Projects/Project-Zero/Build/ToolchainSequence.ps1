# Frontier/Projects/Project-Zero/Build/ToolchainSequence.ps1
#   Builds Project-Zero with cl.exe / lib.exe / link.exe directly.
#   Compatible with Windows PowerShell 5.1 and PowerShell 7+.
#
#     powershell -File Projects\Project-Zero\Build\ToolchainSequence.ps1
#     powershell -File Projects\Project-Zero\Build\ToolchainSequence.ps1 -Configuration Debug
#     powershell -File Projects\Project-Zero\Build\ToolchainSequence.ps1 -Rebuild -Run

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [switch] $Rebuild,
    [switch] $Run,
    [int]    $Parallel = 0,
    # Instruction set of the OLDEST machine this binary must run on — not the machine compiling it.
    #    SSE2    baseline x64: runs anywhere. Use this when unsure.
    #    AVX     Sandy Bridge i5/i7 and later. ⚠️ Sandy Bridge Core i3 (e.g. i3-2120) has NO AVX — it will
    #            crash at launch with 0xc000001d STATUS_ILLEGAL_INSTRUCTION on the first VEX instruction.
    #    AVX2    Haswell (2013) and later.
    # This must match Scripts/BuildJolt.ps1 and every other project script: Jolt derives JPH_USE_AVX/SSE4_2/SSE4_1
    #    from the compiler's __AVX__ macros and RegisterTypes() aborts on a library/client mismatch.
    [ValidateSet('SSE2', 'AVX', 'AVX2')] [string] $Isa = 'SSE2'
)

$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$EngineRoot     = Join-Path $RepositoryRoot 'Engine'
$PackageRoot    = Join-Path $RepositoryRoot 'ExternalPackages'
$ScriptRoot     = Join-Path $RepositoryRoot 'Scripts'
$ProjectRoot    = Join-Path $RepositoryRoot 'Projects\Project-Zero'
$OutputRoot     = Join-Path $ProjectRoot    "Build\Output\Windows\$Configuration"

$script:GlfwBuilt   = $false
$script:ThorVGBuilt = $false

#---
#                                        CONSOLE REPORTING
#---

function Write-Report
{
    param([string] $Tag, [System.ConsoleColor] $Colour, [string] $Message)
    Write-Host ("[$Tag]".PadRight(10)) -ForegroundColor $Colour -NoNewline
    Write-Host " $Message"
}

function Write-Building([string] $Message) { Write-Report -Tag 'Build'    -Colour DarkGray -Message $Message }
function Write-Skipped([string]  $Message) { Write-Report -Tag 'SKIP'     -Colour Cyan     -Message $Message }
function Write-Rejected([string] $Message) { Write-Report -Tag 'FAILED'   -Colour Red      -Message $Message }
function Write-Produced([string] $Message) { Write-Report -Tag 'Compiled' -Colour Green    -Message $Message }
function Write-Lowered([string]  $Message) { Write-Report -Tag 'SPIR-V'   -Colour Magenta  -Message $Message }

#---
#                                       TOOLCHAIN ACQUISITION
#---

function Import-ToolchainEnvironment
{
    if (Get-Command cl.exe -ErrorAction SilentlyContinue)
    {
        Write-Skipped 'toolchain already on PATH'
        return
    }

    $Candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat'
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
    )

    $Selected = $null
    foreach ($Candidate in $Candidates)
    {
        if (Test-Path $Candidate)
        {
            $Selected = $Candidate
            break
        }
    }

    if ($Selected -eq $null)
    {
        throw 'no vcvarsall.bat was found; the C++ toolchain is not installed where this script looks'
    }

    Write-Building "toolchain $Selected"

    $Captured = cmd.exe /c "`"$Selected`" x64 > nul & set"

    foreach ($Line in $Captured)
    {
        if ($Line -match '^([^=]+)=(.*)$')
        {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue))
    {
        throw 'vcvarsall.bat ran but cl.exe is still absent from PATH'
    }
}

function Resolve-VulkanRoot
{
    if ($env:VULKAN_SDK -and (Test-Path $env:VULKAN_SDK))
    {
        return $env:VULKAN_SDK
    }

    $Installed = Get-ChildItem 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue |
                 Sort-Object Name -Descending |
                 Select-Object -First 1

    if ($Installed -eq $null)
    {
        throw 'no Vulkan SDK was found; VULKAN_SDK is unset and C:\VulkanSDK holds nothing'
    }

    return $Installed.FullName
}

#---
#                                         COMPILATION FLAGS
#---

function Get-CompilationFlags([string] $Selection)
{
    $MpFlag = '/MP'
    if ($Parallel -gt 0) { $MpFlag = "/MP$Parallel" }

    $Common = @(
        '/nologo'
        '/c'
        '/EHsc'
        $MpFlag
        '/MD'
        '/std:c++20'
        '/permissive-'
        '/fp:precise'
        '/W4'
        '/utf-8'
        '/Zc:__cplusplus'
        '/DWIN32_LEAN_AND_MEAN'
        '/DNOMINMAX'
        '/D_CRT_SECURE_NO_WARNINGS'   # third-party C (cgltf) uses fopen/strcpy; deprecation warnings are noise
        '/DGLFW_DLL'
        '/DFRONTIER_DEVELOPMENT'
        '/DFRONTIER_ENABLE_GLFW'
    )
    # Baseline SSE2 emits no /arch at all (it is the x64 default); anything else is opt-in via -Isa.
    #    tinybvh falls back to its scalar path cleanly when AVX is absent.
    if ($Isa -ne 'SSE2') { $Common += "/arch:$Isa" }

    if ($Selection -eq 'Debug')
    {
        return $Common + @('/Od', '/Zi', '/Zf', '/DFRONTIER_DEBUG=1')
    }

    return $Common + @('/O2', '/Zi', '/Zf', '/DNDEBUG')
}

#---
#                                           INCLUDE PATHS
#---

function Get-IncludePaths([string] $VulkanRoot)
{
    return @(
        "/I$RepositoryRoot"
        "/I$EngineRoot"
        "/I$(Join-Path $ProjectRoot 'Source')"
        "/I$(Join-Path $VulkanRoot  'Include')"
        "/I$(Join-Path $PackageRoot 'miniaudio')"
        "/I$(Join-Path $RepositoryRoot 'Projects\Project-Dyno\Source')"
        "/I$(Join-Path $PackageRoot 'imgui')"
        "/I$(Join-Path $PackageRoot 'imgui\backends')"
        "/I$(Join-Path $PackageRoot 'glfw\include')"
        "/I$(Join-Path $PackageRoot 'thorvg\inc')"
        "/I$(Join-Path $PackageRoot 'tomlpp\include')"
        "/I$(Join-Path $PackageRoot 'jolt')"
        "/I$(Join-Path $PackageRoot 'cgltf')"
        "/I$(Join-Path $PackageRoot 'tinybvh')"
        "/I$(Join-Path $PackageRoot 'stb')"
        "/I$(Join-Path $PackageRoot 'ufbx')"
        "/I$(Join-Path $PackageRoot 'fast_obj')"
    )
}

#---
#                                          RESPONSE FILES
#---

function Write-ResponseFile([string] $ResponsePath, [string[]] $Arguments)
{
    $Lines = New-Object System.Collections.Generic.List[string]

    foreach ($Argument in $Arguments)
    {
        if ($Argument -notmatch '[ \t"]')
        {
            $Lines.Add($Argument)
        }
        else
        {
            $Trailing = 0
            while ($Trailing -lt $Argument.Length -and
                   $Argument[$Argument.Length - 1 - $Trailing] -eq '\')
            {
                $Trailing++
            }
            $Lines.Add('"' + $Argument + ('\' * $Trailing) + '"')
        }
    }

    [System.IO.File]::WriteAllText($ResponsePath, ($Lines -join "`r`n"), [System.Text.Encoding]::ASCII)
}

#---
#                                       TRANSLATION FRESHNESS
#---

function Test-ObjectFresh([string] $ObjectPath, [string] $SourcePath, [string] $DependencyPath)
{
    if ($Rebuild)                     { return $false }
    if (-not (Test-Path $ObjectPath)) { return $false }
    if (-not (Test-Path $SourcePath)) { return $false }

    $ObjectWritten = (Get-Item $ObjectPath).LastWriteTimeUtc

    if ($ObjectWritten -le (Get-Item $SourcePath).LastWriteTimeUtc) { return $false }
    if (-not (Test-Path $DependencyPath))                           { return $false }

    try
    {
        $Recorded = Get-Content $DependencyPath -Raw | ConvertFrom-Json
        $Included = $Recorded.Data.Includes
    }
    catch { return $false }

    if ($Included -eq $null) { return $false }

    foreach ($Header in $Included)
    {
        if (-not (Test-Path $Header))                                       { return $false }
        if ((Get-Item $Header).LastWriteTimeUtc -ge $ObjectWritten)         { return $false }
    }

    return $true
}

#---
#                                           TRANSLATION
#---

function Invoke-Translation([string[]] $Sources, [string] $Label, [string] $ObjectRoot, [string[]] $Flags, [string[]] $IncludePaths)
{
    if (-not (Test-Path $ObjectRoot))
    {
        New-Item -ItemType Directory -Force -Path $ObjectRoot | Out-Null
    }

    $DependencyRoot = Join-Path $ObjectRoot 'Dependency'
    if (-not (Test-Path $DependencyRoot))
    {
        New-Item -ItemType Directory -Force -Path $DependencyRoot | Out-Null
    }

    $Produced = New-Object System.Collections.Generic.List[string]
    $Stale    = New-Object System.Collections.Generic.List[string]

    foreach ($Source in $Sources)
    {
        $Stem           = [System.IO.Path]::GetFileNameWithoutExtension($Source)
        $ObjectPath     = Join-Path $ObjectRoot "$Stem.obj"
        $DependencyPath = Join-Path $ObjectRoot "$Stem.deps.json"
        $Produced.Add($ObjectPath)

        if (-not (Test-ObjectFresh $ObjectPath $Source $DependencyPath))
        {
            $Stale.Add($Source)
        }
    }

    if ($Stale.Count -eq 0)
    {
        Write-Skipped "$Label unchanged"
        return $Produced.ToArray()
    }

    $Arguments = New-Object System.Collections.Generic.List[string]
    foreach ($F in $Flags)        { $Arguments.Add($F) }
    foreach ($I in $IncludePaths) { $Arguments.Add($I) }
    $Arguments.Add('/Fo' + $ObjectRoot + '\')
    $Arguments.Add("/Fd$(Join-Path $ObjectRoot 'ProjectZero.pdb')")
    $Arguments.Add('/sourceDependencies' + $DependencyRoot + '\')
    foreach ($S in $Stale)        { $Arguments.Add($S) }

    $ResponsePath = Join-Path $ObjectRoot 'ProjectZero.rsp'
    Write-ResponseFile $ResponsePath $Arguments.ToArray()

    Write-Building "$Label - translating $($Stale.Count) of $($Sources.Count)"

    $Diagnostics = & cl.exe '/nologo' "@$ResponsePath"
    $Rejected    = $LASTEXITCODE -ne 0

    $Notable = $Diagnostics | Where-Object { $_ -match ': (warning|error) ' -or $_ -match 'fatal error' }
    if ($Notable) { $Notable | ForEach-Object { Write-Host "    $_" } }

    if ($Rejected)
    {
        if ((-not $Notable) -and $Diagnostics) { $Diagnostics | ForEach-Object { Write-Host "    $_" } }
        Write-Rejected "$Label - cl.exe rejected the translation batch"
        throw "$Label - cl.exe rejected the translation batch"
    }

    foreach ($Source in $Stale)
    {
        $Stem              = [System.IO.Path]::GetFileNameWithoutExtension($Source)
        $FileName          = [System.IO.Path]::GetFileName($Source)
        $WrittenCandidateA = Join-Path $DependencyRoot "$FileName.json"
        $WrittenCandidateB = Join-Path $DependencyRoot "$Stem.json"
        $Wanted            = Join-Path $ObjectRoot     "$Stem.deps.json"

        if (Test-Path $WrittenCandidateA)
        {
            Move-Item $WrittenCandidateA $Wanted -Force
        }
        elseif (Test-Path $WrittenCandidateB)
        {
            Move-Item $WrittenCandidateB $Wanted -Force
        }
    }

    return $Produced.ToArray()
}

#---
#                                         SHADER LOWERING  (.slang -> SPIR-V)
#---

function Resolve-ShaderCompiler([string] $VulkanRoot)
{
    # Prefer glslc for GLSL shaders, fall back to slangc
    $Glslc  = Join-Path $VulkanRoot 'Bin\glslc.exe'
    $Slangc = Join-Path $VulkanRoot 'Bin\slangc.exe'

    if (Test-Path $Glslc)  { return $Glslc  }
    if (Test-Path $Slangc) { return $Slangc }

    throw "the Vulkan SDK at $VulkanRoot carries no shader compiler (glslc.exe or slangc.exe)"
}

# Shader table: source (under Engine\Shaders), glslc stage, output .spv. Every file includes SceneRecords.slang except the
# kernel's own includes; the include list below re-lowers all of them when any shared header changes.
$ShaderTable = @(
    @{ Source = 'ReSTIRViewport.slang';        Stage = 'compute';  Output = 'ReSTIRViewport.spv' }
    @{ Source = 'ClusterCull.slang';           Stage = 'compute';  Output = 'ClusterCull.spv' }
    @{ Source = 'HiZReduce.slang';             Stage = 'compute';  Output = 'HiZReduce.spv' }
    @{ Source = 'SurfaceResolve.slang';        Stage = 'compute';  Output = 'SurfaceResolve.spv' }
    @{ Source = 'VisibilityRaster.vert.slang'; Stage = 'vertex';   Output = 'VisibilityRaster.vert.spv' }
    @{ Source = 'VisibilityRaster.frag.slang'; Stage = 'fragment'; Output = 'VisibilityRaster.frag.spv' }
    @{ Source = 'InterfaceRaster.vert.slang';  Stage = 'vertex';   Output = 'InterfaceRaster.vert.spv' }
    @{ Source = 'InterfaceRaster.frag.slang';  Stage = 'fragment'; Output = 'InterfaceRaster.frag.spv' }
)
$ShaderIncludeNames = @('SceneRecords.slang', 'RayGeneration.slang', 'TraversalCWBVH.slang', 'InterfaceRecords.slang', 'InterfaceSignedDistance.slang')

function Invoke-ShaderLowering([string] $VulkanRoot)
{
    $SpirvRoot = Join-Path $EngineRoot 'Shaders'
    $Compiler  = Resolve-ShaderCompiler $VulkanRoot
    $CompilerName = [System.IO.Path]::GetFileName($Compiler)

    $IncludeStamp = [DateTime]::MinValue
    foreach ($Name in $ShaderIncludeNames)
    {
        $Include = Join-Path $SpirvRoot $Name
        if ((Test-Path $Include) -and ((Get-Item $Include).LastWriteTimeUtc -gt $IncludeStamp))
        {
            $IncludeStamp = (Get-Item $Include).LastWriteTimeUtc
        }
    }

    foreach ($Entry in $ShaderTable)
    {
        $SlangSrc  = Join-Path $SpirvRoot $Entry.Source
        $SpirvPath = Join-Path $SpirvRoot $Entry.Output
        if (-not (Test-Path $SlangSrc))
        {
            Write-Rejected "Engine\Shaders\$($Entry.Source) is missing"
            throw "Engine\Shaders\$($Entry.Source) is missing"
        }

        $Fresh = (-not $Rebuild) -and (Test-Path $SpirvPath) -and
                 ((Get-Item $SpirvPath).LastWriteTimeUtc -gt (Get-Item $SlangSrc).LastWriteTimeUtc) -and
                 ((Get-Item $SpirvPath).LastWriteTimeUtc -gt $IncludeStamp)
        if ($Fresh)
        {
            Write-Skipped "$($Entry.Source) and its includes unchanged"
            continue
        }

        Write-Building "Lowering $($Entry.Source) -> $($Entry.Output)"

        if ($CompilerName -eq 'glslc.exe')
        {
            # glslc requires a recognized extension (.glsl/.vert/.frag/.comp); stage is passed explicitly
            $TempSrc = Join-Path $SpirvRoot ([System.IO.Path]::GetFileNameWithoutExtension($Entry.Output) + '_glslc.glsl')
            Copy-Item $SlangSrc $TempSrc -Force
            $Arguments = @(
                '-DFRONTIER_SHADER_TOOLCHAIN=1'
                "-I$EngineRoot"
                "-I$SpirvRoot"
                '--target-env=vulkan1.2'
                "-fshader-stage=$($Entry.Stage)"
                '-o'
                $SpirvPath
                $TempSrc
            )
        }
        else
        {
            $Arguments = @(
                $SlangSrc
                '-DFRONTIER_SHADER_TOOLCHAIN=1'
                "-I$EngineRoot"
                "-I$SpirvRoot"
                '-target'
                'spirv'
                '-profile'
                'glsl_450'
                '-stage'
                $Entry.Stage
                '-entry'
                'main'
                '-o'
                $SpirvPath
            )
        }

        & $Compiler @Arguments | ForEach-Object { Write-Host "    $_" }

        if ($LASTEXITCODE -ne 0)
        {
            Write-Rejected "$CompilerName rejected $($Entry.Source)"
            throw "$CompilerName rejected $($Entry.Source)"
        }

        Write-Lowered $SpirvPath
    }
}

#---
#                                     DEPENDENCY BUILD SCRIPTS
#---

function Invoke-DependencyScript([string] $ScriptPath, [string[]] $Arguments)
{
    # Works on both PS5.1 and PS7 - call powershell.exe explicitly so the
    # sub-script also runs under whichever host is available.
    $Host51 = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

    if (Test-Path $Host51)
    {
        & $Host51 -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments
    }
    else
    {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments
    }

    return $LASTEXITCODE
}

#---
#                                           THE RUN
#---

Write-Host "Project-Zero - $Configuration"

Import-ToolchainEnvironment
$VulkanRoot = Resolve-VulkanRoot
Write-Building "Vulkan SDK $VulkanRoot"

# Ensure submodules are present -- all 12 packages, soft on network/SSL failure
Write-Building 'Ensuring ExternalPackages submodules are initialised...'
Push-Location $RepositoryRoot
$SubmoduleList = @(
    'ExternalPackages/imgui'
    'ExternalPackages/glfw'
    'ExternalPackages/thorvg'
    'ExternalPackages/tomlpp'
    'ExternalPackages/jolt'
    'ExternalPackages/ufbx'
    'ExternalPackages/earcut'
    'ExternalPackages/cgltf'
    'ExternalPackages/tinybvh'
    'ExternalPackages/clipper2'
    'ExternalPackages/stb'
    'ExternalPackages/miniaudio'
    'ExternalPackages/fast_obj'
)
# Pass 1 -- try normal update (will use cached objects when already checked out)
$ErrorActionBak = $ErrorActionPreference
$ErrorActionPreference = 'SilentlyContinue'
& git submodule update --init -- $SubmoduleList 2>&1 | Out-Null
$UpdateOk = $LASTEXITCODE -eq 0
$ErrorActionPreference = $ErrorActionBak

if (-not $UpdateOk)
{
    # Pass 2 -- SSL/network error; retry once with verification disabled
    Write-Skipped 'git submodule update failed; retrying with GIT_SSL_NO_VERIFY=1 ...'
    $env:GIT_SSL_NO_VERIFY = '1'
    $ErrorActionBak2 = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    & git submodule update --init -- $SubmoduleList 2>&1 | Out-Null
    $UpdateOk = $LASTEXITCODE -eq 0
    $ErrorActionPreference = $ErrorActionBak2
    Remove-Item Env:\GIT_SSL_NO_VERIFY -ErrorAction SilentlyContinue
}

if (-not $UpdateOk)
{
    # [NOTE] git submodule update fails when:
    #    a) network/SSL is unavailable even with verification disabled
    #    b) the user did  git clone  without  --recurse-submodules
    #
    # In either case the directories may already be populated (manual clone or
    # prior successful init).  We check that every listed package directory is
    # non-empty -- anything with at least one file is considered present.
    # An empty or absent directory means the submodule is genuinely missing.
    $Missing = New-Object System.Collections.Generic.List[string]
    foreach ($Sub in $SubmoduleList)
    {
        $SubPath  = Join-Path $RepositoryRoot $Sub
        $HasFiles = (Test-Path $SubPath) -and
                    ((Get-ChildItem $SubPath -Force -ErrorAction SilentlyContinue | Measure-Object).Count -gt 0)
        if (-not $HasFiles) { $Missing.Add($Sub) }
    }
    if ($Missing.Count -gt 0)
    {
        Write-Rejected 'git submodule update failed; these directories are absent or empty:'
        $Missing | ForEach-Object { Write-Host "    $_" }
        Pop-Location
        throw 'git submodule update failed and one or more ExternalPackages directories are missing'
    }
    Write-Skipped "git submodule update non-zero but all $($SubmoduleList.Count) package directories are present -- continuing"
}
Pop-Location

# Build GLFW DLL if absent
$GlfwLib = Join-Path $PackageRoot 'glfw\lib-vc2022\glfw3dll.lib'
if ((-not (Test-Path $GlfwLib)) -and (-not $script:GlfwBuilt))
{
    Write-Building 'GLFW binaries absent - invoking BuildGLFW.ps1'
    $ExitCode = Invoke-DependencyScript (Join-Path $ScriptRoot 'BuildGLFW.ps1') @()
    if ($ExitCode -ne 0) { throw 'BuildGLFW.ps1 failed' }
    $script:GlfwBuilt = $true
}

# Build ThorVG static lib if absent
$ThorVGLib = Join-Path $PackageRoot 'thorvg\lib\thorvg.lib'
if ((-not (Test-Path $ThorVGLib)) -and (-not $script:ThorVGBuilt))
{
    Write-Building 'ThorVG library absent - invoking BuildThorVG.ps1'
    $ExitCode = Invoke-DependencyScript (Join-Path $ScriptRoot 'BuildThorVG.ps1') @('-Configuration', $Configuration)
    if ($ExitCode -ne 0) { throw 'BuildThorVG.ps1 failed' }
    $script:ThorVGBuilt = $true
}

# Build Jolt static lib if absent (D4: rigid bodies drive instance transforms)
#    -Isa is forwarded: Jolt derives JPH_USE_AVX/SSE4_2/SSE4_1 from the compiler macros and RegisterTypes()
#    aborts at run time if the library and this client disagree.
$JoltLib = Join-Path $PackageRoot "jolt\lib\$Configuration\Jolt.lib"
if (-not (Test-Path $JoltLib))
{
    Write-Building 'Jolt library absent - invoking BuildJolt.ps1'
    $ExitCode = Invoke-DependencyScript (Join-Path $ScriptRoot 'BuildJolt.ps1') @('-Configuration', $Configuration, '-Isa', $Isa)
    if ($ExitCode -ne 0) { throw 'BuildJolt.ps1 failed' }
}

# Lower shaders
Invoke-ShaderLowering $VulkanRoot

# Prepare output directory
if ($Rebuild -and (Test-Path $OutputRoot))
{
    Remove-Item (Join-Path $OutputRoot 'Object') -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$ObjectRoot = Join-Path $OutputRoot 'Object'

$Flags        = Get-CompilationFlags $Configuration
$IncludePaths = Get-IncludePaths $VulkanRoot

# Collect sources
$ImGuiSources = @(
    (Join-Path $PackageRoot 'imgui\imgui.cpp')
    (Join-Path $PackageRoot 'imgui\imgui_draw.cpp')
    (Join-Path $PackageRoot 'imgui\imgui_tables.cpp')
    (Join-Path $PackageRoot 'imgui\imgui_widgets.cpp')
    (Join-Path $PackageRoot 'imgui\backends\imgui_impl_glfw.cpp')
    (Join-Path $PackageRoot 'imgui\backends\imgui_impl_vulkan.cpp')
)

$EngineRelative = @(
    # NOTE: this list must match the .cpp files actually in the tree (branch arena/01a06c54-slate, 2026-09-04).
    # Phantom entries from a foreign module layout were removed and the two missing DisplayPresentation files
    # added; the existence guard below fails fast with names if the list ever rots again.
    'Engine\DeviceExchange\SwapchainExchange.cpp'
    'Engine\DeviceExchange\RayTracingCapabilitySet.cpp'
    'Engine\DeviceExchange\InputExchange.cpp'
    'Engine\DeviceExchange\DiagnosticMetrics.cpp'
    'Engine\DeviceExchange\OrientationClassifier.cpp'
    'Engine\DisplayPresentation\ReSTIRIntegrator.cpp'
    'Engine\DisplayPresentation\ShadingTableCodec.cpp'
    'Engine\DisplayPresentation\RenderScheduler.cpp'
    'Engine\DisplayPresentation\ThemeStructure.cpp'
    'Engine\DisplayPresentation\VectorCodec.cpp'
    'Engine\DisplayPresentation\ControlCentreHost.cpp'
    'Engine\DisplayPresentation\FontCodec.cpp'
    'Engine\DisplayPresentation\PixelSpace.cpp'
    'Engine\DisplayPresentation\MotionIntegrator.cpp'
    'Engine\DisplayPresentation\GlyphSpace.cpp'
    'Engine\DisplayPresentation\NotificationQueue.cpp'
    'Engine\DisplayPresentation\TelemetryMetrics.cpp'
    'Engine\DisplayPresentation\ControlKit.cpp'
    'Engine\DisplayPresentation\DialogueHost.cpp'
    'Engine\DisplayPresentation\AppearanceInspector.cpp'
    'Engine\DisplayPresentation\ConfigurationInspector.cpp'
    'Engine\DisplayPresentation\ConfigurationRegistry.cpp'
    'Engine\DisplayPresentation\TypefaceRegistry.cpp'
    'Engine\DisplayPresentation\FidelityClassifier.cpp'
    'Engine\GeometricRaster\CameraProjection.cpp'
    'Engine\GeometricRaster\GeometryStructure.cpp'
    'Engine\GeometricRaster\SceneStructure.cpp'
    'Engine\GeometricRaster\TraversalIndex.cpp'
    'Engine\DeviceExchange\VisibilityExchange.cpp'
    'Engine\DisplayPresentation\DiagnosticInspector.cpp'
    'Engine\ContentInterchange\MaterialIndex.cpp'
    'Engine\ContentInterchange\MaterialCodec.cpp'
    'Engine\ContentInterchange\TextureIndex.cpp'
    'Engine\ContentInterchange\SceneCodec.cpp'
    'Engine\ContentInterchange\ShaderBallStructure.cpp'
    'Engine\ContentInterchange\FbxCodec.cpp'
    'Engine\ContentInterchange\ObjCodec.cpp'
    'Engine\ContentInterchange\ContentCodec.cpp'
    'Engine\ContentInterchange\UfbxTranslation.cpp'
    'Engine\SpatialInterface\InterfaceStructure.cpp'
    'Engine\SpatialInterface\InterfaceSequence.cpp'
    'Engine\SpatialInterface\InterfaceLayoutCodec.cpp'
    'Engine\SpatialInterface\PaletteConfiguration.cpp'
    'Engine\SpatialInterface\InterfacePointerProjection.cpp'
    'Engine\SpatialInterface\InterfaceTextProjection.cpp'
    'Engine\SpatialInterface\InterfaceScreenSequence.cpp'
    'Engine\SpatialInterface\InterfaceVectorCodec.cpp'
    'Engine\SpatialInterface\InterfaceLightProjection.cpp'
    'Engine\DeviceExchange\InterfaceExchange.cpp'
    'Projects\Project-Zero\Source\InterfaceTrialSequence.cpp'
    'Projects\Project-Zero\Source\InstanceMotionSequence.cpp'
    'Projects\Project-Zero\Source\PhysicsInstanceSequence.cpp'
    'Projects\Project-Zero\Source\InterfaceAudioSequence.cpp'
    'Projects\Project-Dyno\Source\CrankClickIntegrator.cpp'
    'Projects\Project-Dyno\Source\DynoSequence.cpp'
    'Engine\PlatformInterchange\AudioExchange.cpp'
    'Engine\PlatformInterchange\MiniaudioTranslation.cpp'
    'Engine\PlatformInterchange\WaveCodec.cpp'
    'Engine\PhysicalDynamics\RigidBodySolver.cpp'
    'Projects\Project-Zero\Source\ShowroomStructure.cpp'
    'Projects\Project-Zero\Source\RayTracingSolver.cpp'
    'Projects\Project-Zero\Source\FlyThroughSolver.cpp'
    'Projects\Project-Zero\Source\GameExecution.cpp'
)

$EngineSources = New-Object System.Collections.Generic.List[string]
foreach ($Rel in $EngineRelative)
{
    $EngineSources.Add((Join-Path $RepositoryRoot $Rel))
}

# Fail fast with NAMES if the source list ever rots again (was: 73 cascading c1xx C1083s, 2026-09-04).
$MissingSources = @($EngineSources | Where-Object { -not (Test-Path $_) }) + @($ImGuiSources | Where-Object { -not (Test-Path $_) })
if ($MissingSources.Count -gt 0) { throw ('missing source files in the translation batch:' + [Environment]::NewLine + ($MissingSources -join [Environment]::NewLine)) }

$AllSources = New-Object System.Collections.Generic.List[string]
foreach ($S in $EngineSources) { $AllSources.Add($S) }
foreach ($S in $ImGuiSources)  { $AllSources.Add($S) }

# Translate
$ObjectFiles = Invoke-Translation $AllSources.ToArray() 'Project-Zero' $ObjectRoot $Flags $IncludePaths

# Link
$BinaryRoot = Join-Path $OutputRoot 'Binary'
New-Item -ItemType Directory -Force -Path $BinaryRoot | Out-Null

$ExePath = Join-Path $BinaryRoot 'Project-Zero.exe'

# Copy GLFW DLL beside executable
$GlfwDll = Join-Path $PackageRoot 'glfw\lib-vc2022\glfw3.dll'
if (Test-Path $GlfwDll)
{
    try { Copy-Item $GlfwDll $BinaryRoot -Force -ErrorAction Stop }
    catch { if (-not (Test-Path (Join-Path $BinaryRoot 'glfw3.dll'))) { throw $_ } }
}

# Copy the lowered shaders beside the executable so double-clicking the .exe works
# (the runtime searches <cwd>\Engine\Shaders first, then <exe dir>\Engine\Shaders and its parents).
$SpirvTarget = Join-Path $BinaryRoot 'Engine\Shaders'
New-Item -ItemType Directory -Force -Path $SpirvTarget | Out-Null
foreach ($Entry in $ShaderTable)
{
    $SpirvSource = Join-Path $EngineRoot ('Shaders\' + $Entry.Output)
    if (Test-Path $SpirvSource) { Copy-Item $SpirvSource $SpirvTarget -Force }
    else { Write-Rejected "Engine\Shaders\$($Entry.Output) is missing - Project-Zero will fail at bring-up" }
}

if (Test-Path $ExePath)
{
    try
    {
        Remove-Item $ExePath -Force -ErrorAction Stop
    }
    catch
    {
        $Running = Get-Process -Name 'Project-Zero' -ErrorAction SilentlyContinue
        if ($Running) { $Running | Stop-Process -Force }
        Start-Sleep -Milliseconds 200
        Remove-Item $ExePath -Force -ErrorAction Stop
    }
}

$LinkArgs = New-Object System.Collections.Generic.List[string]
$LinkArgs.Add('/nologo')
$LinkArgs.Add('/DEBUG')
$LinkArgs.Add('/SUBSYSTEM:CONSOLE')
$LinkArgs.Add("/OUT:$ExePath")
$LinkArgs.Add("/PDB:$(Join-Path $BinaryRoot 'Project-Zero.pdb')")
foreach ($Obj in $ObjectFiles)                    { $LinkArgs.Add($Obj) }
$LinkArgs.Add((Join-Path $VulkanRoot 'Lib\vulkan-1.lib'))
$LinkArgs.Add((Join-Path $PackageRoot 'glfw\lib-vc2022\glfw3dll.lib'))
$LinkArgs.Add((Join-Path $PackageRoot 'thorvg\lib\thorvg.lib'))
$LinkArgs.Add($JoltLib)
$LinkArgs.Add('gdi32.lib')
$LinkArgs.Add('user32.lib')
$LinkArgs.Add('shell32.lib')

Write-Building 'Linking Project-Zero.exe...'
$Diagnostics = & link.exe @($LinkArgs.ToArray())

if ($LASTEXITCODE -ne 0)
{
    $Diagnostics | ForEach-Object { Write-Host "    $_" }
    Write-Rejected 'link.exe rejected Project-Zero'
    throw 'link.exe rejected Project-Zero'
}

Write-Produced $ExePath

# Mirror the freshly linked binary to <repo>\Build\ so `.\Build\Project-Zero.exe` works from the repository root,
#    which is the command References/RunningTheShowroom.md documents. Copying (rather than only linking here) is what
#    prevents the classic "I rebuilt but the old UI is still there" report: a stale copy from an earlier session would
#    otherwise sit at that path forever, since nothing else ever writes to it.
$RootBinary = Join-Path $RepositoryRoot 'Build'
New-Item -ItemType Directory -Force -Path $RootBinary | Out-Null
foreach ($Payload in @('Project-Zero.exe', 'Project-Zero.pdb', 'glfw3.dll'))
{
    $From = Join-Path $BinaryRoot $Payload
    if (Test-Path $From) { Copy-Item $From $RootBinary -Force -ErrorAction SilentlyContinue }
}
# The runtime searches <cwd>\Engine\Shaders first, so the mirrored copy needs the lowered SPIR-V beside it too.
$RootShaders = Join-Path $RootBinary 'Engine\Shaders'
New-Item -ItemType Directory -Force -Path $RootShaders | Out-Null
foreach ($Entry in $ShaderTable)
{
    $SpirvSource = Join-Path $EngineRoot ('Shaders\' + $Entry.Output)
    if (Test-Path $SpirvSource) { Copy-Item $SpirvSource $RootShaders -Force }
}
Write-Produced (Join-Path $RootBinary 'Project-Zero.exe')

if ($Run)
{
    Write-Building 'Launching Project-Zero (working directory = repository root)...'
    Push-Location $RepositoryRoot
    try     { & "$ExePath"; Write-Building "Project-Zero exited with code $LASTEXITCODE" }
    finally { Pop-Location }
}
