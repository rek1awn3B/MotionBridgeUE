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
$StreamPath = Join-Path $RepositoryRoot `
    "artifacts\realtime\manny_root_motion_stream.json"
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
    throw "未找到 UE 5.6。请传入 -EngineRoot 或设置 UE_ROOT。"
}
if (-not (Test-Path -LiteralPath $StreamPath))
{
    throw "实时演示数据不存在：$StreamPath"
}

$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$ServerScript = "tools\motionbridge_websocket_server.py"
$RelativeStream = "artifacts\realtime\manny_root_motion_stream.json"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Invoke-RealtimeCase
{
    param(
        [string]$Name,
        [string[]]$ServerExtraArguments,
        [string[]]$EditorExtraArguments,
        [string]$PassMarker
    )

    $ServerOut = Join-Path $OutputDirectory "$Name-server.out.log"
    $ServerErr = Join-Path $OutputDirectory "$Name-server.err.log"
    $EditorOut = Join-Path $OutputDirectory "$Name-editor.out.log"
    $EditorErr = Join-Path $OutputDirectory "$Name-editor.err.log"
    $EditorLog = Join-Path $OutputDirectory "$Name-editor.log"
    $ServerArguments = @(
        "-3.12",
        $ServerScript,
        "--stream-file",
        $RelativeStream
    ) + $ServerExtraArguments
    $ServerProcess = $null
    try
    {
        $ServerProcess = Start-Process `
            -FilePath "py" `
            -ArgumentList $ServerArguments `
            -WorkingDirectory $RepositoryRoot `
            -WindowStyle Hidden `
            -RedirectStandardOutput $ServerOut `
            -RedirectStandardError $ServerErr `
            -PassThru
        Start-Sleep -Seconds 1
        if ($ServerProcess.HasExited)
        {
            throw "$Name WebSocket 服务启动失败。查看 $ServerErr"
        }

        $EditorArguments = @(
            $ProjectFile,
            "/Game/MotionBridge/Maps/L_MotionBridge100StyleDemo",
            "-game",
            "-nullrhi",
            "-unattended",
            "-nosplash",
            "-nosound",
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
            throw "$Name 超过 $TimeoutSeconds 秒仍未完成。"
        }
        if ($EditorProcess.ExitCode -ne 0)
        {
            throw "$Name UE 退出码为 $($EditorProcess.ExitCode)。"
        }
        if (-not (Select-String -LiteralPath $EditorLog -SimpleMatch $PassMarker))
        {
            throw "$Name 没有产生通过标记：$PassMarker"
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
    -PassMarker "MOTIONBRIDGE_WEBSOCKET_TEST=PASS"

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
