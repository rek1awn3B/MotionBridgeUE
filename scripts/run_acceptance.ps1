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

    Write-Host "`n== C++ lab executables ==" -ForegroundColor Cyan
    $LabCount = 0
    Get-ChildItem -LiteralPath "build-vs\Release" -Filter "*_lab.exe" |
        Sort-Object Name |
        ForEach-Object {
            & $_.FullName | Out-Null
            if ($LASTEXITCODE -ne 0)
            {
                throw "$($_.Name) 失败，退出码：$LASTEXITCODE"
            }
            $LabCount++
            Write-Host "PASS  $($_.Name)"
        }
    if ($LabCount -ne 16)
    {
        throw "预期 16 个 C++ lab，实际找到 $LabCount 个。"
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
