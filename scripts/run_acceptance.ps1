param(
    [string]$EngineRoot = "",
    [switch]$SkipUnreal,
    [switch]$RunRealtime
)

$ErrorActionPreference = "Stop"
$RepositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $RepositoryRoot

function Invoke-Checked
{
    param(
        [string]$Name,
        [scriptblock]$Command
    )

    Write-Host "`n== $Name ==" -ForegroundColor Cyan
    & $Command
    if ($LASTEXITCODE -ne 0)
    {
        throw "$Name 失败，退出码：$LASTEXITCODE"
    }
}

try
{
    Invoke-Checked "Python tests" {
        py -3.12 -m unittest discover -s python -p "test_*.py"
    }

    Invoke-Checked "C++ configure" {
        cmake -S . -B build-vs -A x64
    }

    Invoke-Checked "C++ Release build" {
        cmake --build build-vs --config Release
    }

    Write-Host "`n== C++ motion-system executables ==" -ForegroundColor Cyan
    $LabNames = @(
        "bvh_data_structure_lab",
        "foot_ik_lab",
        "joint_depth_grouping_lab",
        "motion_source_polymorphism_lab",
        "quaternion_interpolation_lab",
        "realtime_frame_buffer_lab",
        "transform3d_lab"
    )
    foreach ($LabName in $LabNames)
    {
        $LabPath = Join-Path "build-vs\Release" "$LabName.exe"
        if (-not (Test-Path -LiteralPath $LabPath))
        {
            throw "缺少 C++ 测试程序：$LabPath"
        }
        & $LabPath | Out-Null
        if ($LASTEXITCODE -ne 0)
        {
            throw "$LabName 失败，退出码：$LASTEXITCODE"
        }
        Write-Host "PASS  $LabName.exe"
    }

    if (-not $SkipUnreal)
    {
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
            throw "未找到 UE 5.6。请传入 -EngineRoot，或使用 -SkipUnreal。"
        }

        $BuildScript = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
        $ProjectPath = Join-Path $RepositoryRoot `
            "Unreal\MotionBridgeDemo\MotionBridgeDemo.uproject"

        Invoke-Checked "UE Game target" {
            & $BuildScript TP_ThirdPerson Win64 Development `
                $ProjectPath -WaitMutex -FromMsBuild
        }
        Invoke-Checked "UE Editor target" {
            & $BuildScript MBEditor Win64 Development `
                $ProjectPath -WaitMutex -FromMsBuild
        }

        if ($RunRealtime)
        {
            Write-Host "`n== UE realtime integration ==" -ForegroundColor Cyan
            & (Join-Path $PSScriptRoot "run_realtime_acceptance.ps1") `
                -EngineRoot $EngineRoot
        }
    }

    Write-Host "`nMOTIONBRIDGE_ACCEPTANCE=PASS" -ForegroundColor Green
}
finally
{
    Pop-Location
}
