#============================================================================================================================================
#                                                    LAUNCHEDITORPREVIEWWINDOWS.PS1
#============================================================================================================================================
# 🧩 Launch a packaged Windows editor preview as a tester does (double-click, then Project Everest) on a software Vulkan driver and
#    keep what it shows: the diagnostic file and a screenshot of the Project Browser and of the Scene Viewport.
#
#    .\Automation\LaunchEditorPreviewWindows.ps1 -Archive Binaries\FrontierSceneViewport-Windows-x64.zip -Evidence Evidence
#
#    Driver: Chrome's SwiftShader with Chrome's Vulkan loader (GitHub's Windows images carry Chrome), else Mesa lavapipe from
#    pal1000/mesa-dist-win with the first loader found. A hardware GPU is not involved: this checks start-up, not speed.

param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$Evidence,
    [ValidateSet('auto', 'swiftshader', 'lavapipe')][string]$Driver = 'auto',
    [int]$BrowserSeconds = 180,
    [int]$ViewportSeconds = 1200,
    [int]$ViewportRefreshes = 4
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
$Evidence = (Resolve-Path $Evidence).Path
$Scratch = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$Extracted = Join-Path $Scratch 'FrontierEditorPreview'
if (Test-Path $Extracted) { Remove-Item -Recurse -Force $Extracted }
Expand-Archive -Path $Archive -DestinationPath $Extracted -Force
$Package = (Get-ChildItem $Extracted -Directory | Select-Object -First 1).FullName
$Executable = Join-Path $Package 'Frontier.exe'
if (-not (Test-Path $Executable)) { throw "No Frontier.exe in $Archive" }
$Summary = [System.Collections.Generic.List[string]]::new()

#------------------------------------------------------------------------------------------------------------------------
#                                                     SOFTWARE VULKAN DRIVER
#------------------------------------------------------------------------------------------------------------------------

$ChromeRoot = 'C:\Program Files\Google\Chrome\Application'
$ChromeIcd = if (Test-Path $ChromeRoot) {
    Get-ChildItem $ChromeRoot -Recurse -Filter 'vk_swiftshader_icd.json' -ErrorAction SilentlyContinue | Select-Object -First 1
}
$Loader = @(
    $(if ($ChromeIcd) { Join-Path $ChromeIcd.DirectoryName 'vulkan-1.dll' }),
    (Join-Path $env:WINDIR 'System32\vulkan-1.dll')
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $Loader) { throw 'No Vulkan loader (vulkan-1.dll) in Chrome or System32' }

if ($Driver -eq 'swiftshader' -or ($Driver -eq 'auto' -and $ChromeIcd)) {
    if (-not $ChromeIcd) { throw "Chrome's SwiftShader was not found under $ChromeRoot" }
    $Icd = $ChromeIcd.FullName
    $DriverName = "SwiftShader from Chrome $(Split-Path $ChromeIcd.DirectoryName -Leaf)"
}
else {
    $Mesa = Join-Path $Scratch 'Mesa'
    $MesaArchive = Join-Path $Scratch 'mesa3d-26.2.5-release-msvc.7z'
    Invoke-WebRequest -Uri 'https://github.com/pal1000/mesa-dist-win/releases/download/26.2.5/mesa3d-26.2.5-release-msvc.7z' -OutFile $MesaArchive
    & 7z x $MesaArchive "-o$Mesa" 'x64\vulkan_lvp.dll' 'x64\lvp_icd.x86_64.json' -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Mesa lavapipe extraction failed' }
    $Icd = Join-Path $Mesa 'x64\lvp_icd.x86_64.json'
    $DriverName = 'Mesa 26.2.5 lavapipe (pal1000/mesa-dist-win)'
}
# 📝 Frontier.exe loads vulkan-1.dll at run time; its own folder comes first in the DLL search order.
Copy-Item $Loader (Join-Path $Package 'vulkan-1.dll') -Force
$env:VK_DRIVER_FILES = $Icd
$env:VK_ICD_FILENAMES = $Icd
$Summary.Add("Driver: $DriverName; loader $Loader")

#------------------------------------------------------------------------------------------------------------------------
#                                                     LAUNCH, WAIT, CAPTURE
#------------------------------------------------------------------------------------------------------------------------

$Logs = Join-Path $env:LOCALAPPDATA 'Frontier\Logs'

function Save-Screen([string]$Path) {
    $Bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $Bitmap = New-Object System.Drawing.Bitmap $Bounds.Width, $Bounds.Height
    $Graphics = [System.Drawing.Graphics]::FromImage($Bitmap)
    $Graphics.CopyFromScreen($Bounds.Location, [System.Drawing.Point]::Empty, $Bounds.Size)
    $Bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $Graphics.Dispose()
    $Bitmap.Dispose()
}

function Read-Journal($Log) {
    if (-not $Log) { return '' }
    # 📝 The editor keeps the file open; share it for reading.
    $Stream = [System.IO.File]::Open($Log.FullName, 'Open', 'Read', 'ReadWrite')
    try { return (New-Object System.IO.StreamReader($Stream)).ReadToEnd() } finally { $Stream.Dispose() }
}

function Invoke-Launch([string]$Name, [string[]]$Arguments, [int]$Seconds, [scriptblock]$Reached) {
    $Earlier = @(Get-ChildItem $Logs -Filter 'Frontier-*.log' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    $Start = @{ FilePath = $Executable; WorkingDirectory = $Package; PassThru = $true }
    if ($Arguments) { $Start.ArgumentList = $Arguments }
    $Process = Start-Process @Start
    $Deadline = (Get-Date).AddSeconds($Seconds)
    $Log = $null
    $Text = ''
    $Outcome = 'timed out'
    while ((Get-Date) -lt $Deadline) {
        Start-Sleep -Seconds 5
        if (-not $Log) {
            $Log = Get-ChildItem $Logs -Filter 'Frontier-*.log' -ErrorAction SilentlyContinue |
                Where-Object { $Earlier -notcontains $_.FullName } | Sort-Object LastWriteTime | Select-Object -Last 1
        }
        $Text = Read-Journal $Log
        if ($Text -match '\[SEVERE\]|\[SceneViewportRefused\]') { $Outcome = 'failed'; break }
        if (& $Reached $Text) { $Outcome = 'reached'; break }
        if ($Process.HasExited) { $Outcome = "exited with $($Process.ExitCode)"; break }
    }
    Start-Sleep -Seconds 4
    Save-Screen (Join-Path $Evidence "$Name.png")
    if (-not $Process.HasExited) { Stop-Process -Id $Process.Id -Force }
    Start-Sleep -Seconds 2
    $Text = Read-Journal $Log
    if ($Log) { Copy-Item $Log.FullName (Join-Path $Evidence "$Name.log") }
    return @{ Outcome = $Outcome; Text = $Text }
}

# ① Double-click: the Project Browser initializes its preview rendering and confirms its first thumbnails.
$Browser = Invoke-Launch 'ProjectBrowser' @() $BrowserSeconds {
    param($Text) $Text -match 'Browser preview rendering initialized' -and $Text -match '\[BrowserConfirm\]'
}
$Summary.Add("Project Browser: $($Browser.Outcome)")

# ② Project Everest from the command line: the Scene Viewport brings the ported GI and the environment up and refreshes.
$Project = Join-Path $Package 'Projects\Everest\Everest.projectspace'
$Viewport = Invoke-Launch 'EverestSceneViewport' @("`"$Project`"") $ViewportSeconds {
    param($Text) ([regex]::Matches($Text, '\[SceneViewport\] image ')).Count -ge $ViewportRefreshes
}
$Summary.Add("Everest Scene Viewport: $($Viewport.Outcome)")

#------------------------------------------------------------------------------------------------------------------------
#                                                     REPORT
#------------------------------------------------------------------------------------------------------------------------

$Interesting = '\[(Startup|Build|Window|GLFW|VulkanDevice|VulkanError|SEVERE|BrowserPreviewRefused|ProjectSelection|SceneViewport|SceneViewportRefused|FrameConstructError)\]'
foreach ($Phase in @(@('ProjectBrowser', $Browser), @('EverestSceneViewport', $Viewport))) {
    $Summary.Add('')
    $Summary.Add("$($Phase[0]) journal:")
    foreach ($Line in ($Phase[1].Text -split "`n" | Where-Object { $_ -match $Interesting } | Select-Object -First 24)) {
        $Summary.Add('    ' + $Line.Trim())
    }
}
$Summary | Set-Content -Encoding utf8 (Join-Path $Evidence 'Summary.txt')
$Summary | ForEach-Object { Write-Output $_ }
# 📝 GitHub keeps annotations with the run: the outcome stays readable without downloading the evidence.
if ($env:GITHUB_ACTIONS) {
    Write-Output "::notice title=Windows launch::$DriverName"
    Write-Output "::notice title=Project Browser::$($Browser.Outcome)"
    Write-Output "::notice title=Everest Scene Viewport::$($Viewport.Outcome)"
    $Last = ($Viewport.Text -split "`n" | Where-Object { $_ -match '\[SceneViewport\]' } | Select-Object -Last 1)
    if ($Last) { Write-Output "::notice title=Last Scene Viewport status::$($Last.Trim())" }
    $Severe = ($Browser.Text + "`n" + $Viewport.Text) -split "`n" | Where-Object { $_ -match '\[SEVERE\]|Refused\]' } | Select-Object -First 3
    foreach ($Line in $Severe) { Write-Output "::error title=Editor failure::$($Line.Trim())" }
}
if ($Browser.Outcome -ne 'reached' -or $Viewport.Outcome -ne 'reached') { exit 1 }
