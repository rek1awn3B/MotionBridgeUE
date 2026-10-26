param(
    [string]$EngineRoot = "",
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"
$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectDirectory = (Resolve-Path (
    Join-Path $RepositoryRoot "Unreal\MotionBridgeDemo"
)).Path
$ProjectFile = (Resolve-Path (
    Join-Path $ProjectDirectory "MotionBridgeDemo.uproject"
)).Path
$BvhPath = Join-Path $RepositoryRoot `
    "data\bvh\100style\Neutral_FW_frames_530_694.bvh"
$CalibrationPath = Join-Path $RepositoryRoot `
    "data\calibration\100style_source_calibration.json"
$MannyCalibrationPath = Join-Path $RepositoryRoot `
    "data\calibration\100style_to_manny_calibration.json"
$OutputDirectory = Join-Path $RepositoryRoot "artifacts\runtime_acceptance"

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

$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$ServerScript = "tools\motionbridge_websocket_server.py"
$RelativeBvh = "data\bvh\100style\Neutral_FW_frames_530_694.bvh"
$RelativeCalibration = "data\calibration\100style_source_calibration.json"
$RelativeMannyCalibration = `
    "data\calibration\100style_to_manny_calibration.json"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Invoke-RealtimeCase
{
    param(
        [string]$Name,
        [string[]]$ServerExtraArguments,
        [string[]]$EditorExtraArguments,
        [string]$PassMarker,
        [string]$AdditionalPassMarker = ""
    )

    $ServerOut = Join-Path $OutputDirectory "$Name-server.out.log"
    $ServerErr = Join-Path $OutputDirectory "$Name-server.err.log"
    $EditorOut = Join-Path $OutputDirectory "$Name-editor.out.log"
    $EditorErr = Join-Path $OutputDirectory "$Name-editor.err.log"
    $EditorLog = Join-Path $OutputDirectory "$Name-editor.log"
    foreach ($PreviousLog in @(
        $ServerOut,
        $ServerErr,
        $EditorOut,
        $EditorErr,
        $EditorLog
    ))
    {
        if (Test-Path -LiteralPath $PreviousLog)
        {
            Remove-Item -LiteralPath $PreviousLog -Force
        }
    }
    $ServerArguments = @(
        $ServerScript,
        "--bvh-file",
        $RelativeBvh,
        "--calibration-file",
        $RelativeCalibration,
        "--manny-calibration-file",
        $RelativeMannyCalibration
    ) + $ServerExtraArguments
    $ServerProcess = $null
    try
    {
        $ServerProcess = Start-Process `
            -FilePath $PythonExe `
            -ArgumentList $ServerArguments `
            -WorkingDirectory $RepositoryRoot `
            -WindowStyle Hidden `
            -RedirectStandardOutput $ServerOut `
            -RedirectStandardError $ServerErr `
            -PassThru
        Start-Sleep -Seconds 1
        if ($ServerProcess.HasExited)
        {
            throw "$Name WebSocket server failed. See $ServerErr"
        }

        $EditorArguments = @(
            $ProjectFile,
            "/Game/MotionBridge/Maps/L_MotionBridge100StyleDemo",
            "-game",
            "-nullrhi",
            "-unattended",
            "-nosplash",
            "-nosound",
            '-ExecCmds="t.MaxFPS 60"',
            "-MotionBridgeExitAfterTest",
            "-stdout",
            "-FullStdOutLogOutput",
            "-abslog=$EditorLog"
        ) + $EditorExtraArguments
        $EditorProcess = Start-Process `
            -FilePath $Editor `
            -ArgumentList $EditorArguments `
            -WorkingDirectory $ProjectDirectory `
            -RedirectStandardOutput $EditorOut `
            -RedirectStandardError $EditorErr `
            -PassThru
        if (-not $EditorProcess.WaitForExit($TimeoutSeconds * 1000))
        {
            Stop-Process -Id $EditorProcess.Id
            throw "$Name did not finish within $TimeoutSeconds seconds."
        }
        # PowerShell 5.1 may leave ExitCode unset after the timed overload.
        $EditorProcess.WaitForExit()
        $EditorProcess.Refresh()
        if ($null -ne $EditorProcess.ExitCode -and $EditorProcess.ExitCode -ne 0)
        {
            throw "$Name UE exited with code $($EditorProcess.ExitCode)."
        }
        if (-not (Select-String -LiteralPath $EditorLog -SimpleMatch $PassMarker))
        {
            throw "$Name did not emit pass marker: $PassMarker"
        }
        if ($AdditionalPassMarker -and -not (
            Select-String -LiteralPath $EditorLog -SimpleMatch $AdditionalPassMarker
        ))
        {
            throw "$Name did not emit pass marker: $AdditionalPassMarker"
        }
        Write-Host "PASS  $Name" -ForegroundColor Green
    }
    finally
    {
        if ($ServerProcess -and -not $ServerProcess.HasExited)
        {
            Stop-Process -Id $ServerProcess.Id
        }
    }
}

Invoke-RealtimeCase `
    -Name "websocket-steady" `
    -ServerExtraArguments @() `
    -EditorExtraArguments @(
        "-MotionBridgePortfolioWalkRuntimeDemo",
        "-MotionBridgeWebSocketTest"
    ) `
    -PassMarker "MOTIONBRIDGE_WEBSOCKET_TEST=PASS" `
    -AdditionalPassMarker "MOTIONBRIDGE_RUNTIME_FOOT_IK=PASS"

Invoke-RealtimeCase `
    -Name "websocket-reconnect" `
    -ServerExtraArguments @("--disconnect-after", "50") `
    -EditorExtraArguments @(
        "-MotionBridgePortfolioWalkRuntimeDemo",
        "-MotionBridgeWebSocketReconnectTest"
    ) `
    -PassMarker "MOTIONBRIDGE_WEBSOCKET_RECONNECT_TEST=PASS"

Invoke-RealtimeCase `
    -Name "morph-target" `
    -ServerExtraArguments @("--facial-demo") `
    -EditorExtraArguments @("-MotionBridgeMorphTargetTest") `
    -PassMarker "MOTIONBRIDGE_MORPH_TARGET_TEST=PASS"

Write-Host "MOTIONBRIDGE_REALTIME_ACCEPTANCE=PASS" -ForegroundColor Green
