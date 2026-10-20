param(
    [string]$EngineRoot = "",
    [switch]$FacialDemo
)

$ErrorActionPreference = "Stop"

$RepositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ProjectPath = Join-Path $RepositoryRoot `
    "Unreal\MotionBridgeDemo\MotionBridgeDemo.uproject"
$ServerPath = Join-Path $RepositoryRoot `
    "tools\motionbridge_websocket_server.py"
$StreamPath = Join-Path $RepositoryRoot `
    "artifacts\realtime\manny_root_motion_stream.json"

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

$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
if (-not (Test-Path -LiteralPath $Editor))
{
    throw "UnrealEditor.exe 不存在：$Editor"
}
if (-not (Test-Path -LiteralPath $StreamPath))
{
    throw "实时演示数据不存在：$StreamPath"
}

$ServerArguments = @("-3.12", $ServerPath, "--stream-file", $StreamPath)
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
}

$ServerProcess = $null
try
{
    $ServerProcess = Start-Process `
        -FilePath "py" `
        -ArgumentList $ServerArguments `
        -WindowStyle Hidden `
        -PassThru
    Start-Sleep -Seconds 1
    if ($ServerProcess.HasExited)
    {
        throw "WebSocket 服务启动失败，退出码：$($ServerProcess.ExitCode)"
    }

    Write-Host "WebSocket 服务已启动：ws://127.0.0.1:8765/"
    Write-Host "关闭 UE 演示窗口后，服务会自动退出。"
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
