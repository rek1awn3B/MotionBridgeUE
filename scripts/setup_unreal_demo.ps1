param(
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.6"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path(
    Join-Path $PSScriptRoot "..\Unreal\MotionBridgeDemo"
)
$TemplateContent = Join-Path $EngineRoot "Templates\TP_ThirdPerson\Content"
$MannequinContent = Join-Path $EngineRoot `
    "Templates\TemplateResources\Standard\Mannequin\Content"
$HighCharactersContent = Join-Path $EngineRoot `
    "Templates\TemplateResources\High\Characters\Content"
$DestinationContent = Join-Path $ProjectRoot "Content"

if (-not (Test-Path -LiteralPath $TemplateContent))
{
    throw "Third Person template content not found: $TemplateContent"
}
if (-not (Test-Path -LiteralPath $MannequinContent))
{
    throw "Standard Mannequin content not found: $MannequinContent"
}
if (-not (Test-Path -LiteralPath $HighCharactersContent))
{
    throw "UE5 character content not found: $HighCharactersContent"
}

New-Item -ItemType Directory -Force -Path $DestinationContent | Out-Null
Copy-Item -Path "$TemplateContent\*" `
    -Destination $DestinationContent -Recurse -Force
Copy-Item -Path "$MannequinContent\*" `
    -Destination $DestinationContent -Recurse -Force
$HighCharactersDestination = Join-Path $DestinationContent "Characters"
New-Item -ItemType Directory -Force `
    -Path $HighCharactersDestination | Out-Null
Copy-Item -Path "$HighCharactersContent\*" `
    -Destination $HighCharactersDestination -Recurse -Force

Write-Host "MotionBridge UE demo content is ready: $DestinationContent"
