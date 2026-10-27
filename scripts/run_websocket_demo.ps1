param(
    [string]$EngineRoot = "",
    [switch]$FacialDemo,
    [switch]$FrontView,
    [switch]$DisableFootIK
)

$ErrorActionPreference = "Stop"

$RepositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ProjectPath = Join-Path $RepositoryRoot `
    "Unreal\MotionBridgeDemo\MotionBridgeDemo.uproject"
$ServerPath = Join-Path $RepositoryRoot `
    "tools\motionbridge_websocket_server.py"
$BvhPath = Join-Path $RepositoryRoot `
    "data\bvh\100style\Neutral_FW_frames_470_694.bvh"
$CalibrationPath = Join-Path $RepositoryRoot `
    "data\calibration\100style_source_calibration.json"
$MannyCalibrationPath = Join-Path $RepositoryRoot `
    "data\calibration\100style_to_manny_calibration.json"

if (-not $EngineRoot)
{
    $Candidates = @(
        $env:UE_ROOT,
        "D:\Program Files\Epic Games\UE_5.6",
        "C:\Program Files\Epic Games\UE_5.6"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    $EngineRoot = $Candidates | Select-Object -First 1
}

if (-not $EngineRoot)
{
    throw "UE 5.6 was not found. Pass -EngineRoot or set UE_ROOT."
}

$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
if (-not (Test-Path -LiteralPath $Editor))
{
    throw "UnrealEditor.exe does not exist: $Editor"
}
if (-not (Test-Path -LiteralPath $BvhPath))
{
    throw "Raw BVH does not exist: $BvhPath"
}
if (-not (Test-Path -LiteralPath $CalibrationPath))
{
    throw "Source skeleton calibration does not exist: $CalibrationPath"
}
if (-not (Test-Path -LiteralPath $MannyCalibrationPath))
{
    throw "Manny retarget calibration does not exist: $MannyCalibrationPath"
}
$PythonCandidates = @(
    $env:MOTIONBRIDGE_PYTHON,
    "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
    (Get-Command python -ErrorAction SilentlyContinue).Source
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
$PythonExe = $PythonCandidates | Select-Object -First 1
if (-not $PythonExe)
{
    throw "Python was not found. Set MOTIONBRIDGE_PYTHON to override."
}

$ServerArguments = @(
    $ServerPath,
    "--bvh-file",
    $BvhPath,
    "--calibration-file",
    $CalibrationPath,
    "--manny-calibration-file",
    $MannyCalibrationPath
)
$EditorArguments = @(
    $ProjectPath,
    "/Game/MotionBridge/Maps/L_MotionBridge100StyleDemo",
    "-game",
    "-windowed",
    "-ResX=1280",
    "-ResY=720",
    "-NoSplash"
)

if ($FacialDemo)
{
    $ServerArguments += "--facial-demo"
    $EditorArguments += "-MotionBridgeMorphTargetTest"
}
else
{
    $EditorArguments += @(
        "-MotionBridgePortfolioWalkRuntimeDemo",
        "-MotionBridgeEnableWebSocket"
    )
    if ($FrontView)
    {
        $EditorArguments += "-MotionBridgeFrontView"
    }
    if ($DisableFootIK)
    {
        $EditorArguments += "-MotionBridgeDisableFootIK"
    }
}

$ServerProcess = $null
try
{
    $ServerProcess = Start-Process `
        -FilePath $PythonExe `
        -ArgumentList $ServerArguments `
        -WindowStyle Hidden `
        -PassThru
    Start-Sleep -Seconds 1
    if ($ServerProcess.HasExited)
    {
        throw "WebSocket server failed with code $($ServerProcess.ExitCode)."
    }

    Write-Host "WebSocket server started: ws://127.0.0.1:8765/"
    Write-Host "The server exits after the UE demo window closes."
    $EditorProcess = Start-Process `
        -FilePath $Editor `
        -ArgumentList $EditorArguments `
        -PassThru
    $EditorProcess.WaitForExit()
}
finally
{
    if ($ServerProcess -and -not $ServerProcess.HasExited)
    {
        Stop-Process -Id $ServerProcess.Id
    }
}
