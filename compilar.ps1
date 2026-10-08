# Compila AlmaLili con MSVC, CMake y Ninja (los tres vienen con Visual Studio).
#
#   .\compilar.ps1                 compila en Release
#   .\compilar.ps1 -Pruebas        compila y ejecuta las pruebas
#   .\compilar.ps1 -Config Debug   compila en Debug
#
# Este archivo se mantiene en ASCII puro porque Windows PowerShell 5.1 no lee
# UTF-8 sin BOM.

param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Pruebas
)

$ErrorActionPreference = 'Stop'
$raiz = $PSScriptRoot
$salida = Join-Path $raiz 'build'

function Comprobar([string]$paso) {
    if ($LASTEXITCODE -ne 0) { throw "Fallo: $paso (codigo $LASTEXITCODE)" }
}

# Si el compilador no esta en el PATH, se carga el entorno de Visual Studio.
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'No se encontro Visual Studio (falta vswhere.exe).' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio no tiene instaladas las herramientas de C++.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

cmake -S $raiz -B $salida -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
Comprobar 'configuracion de CMake'
cmake --build $salida
Comprobar 'compilacion'

if ($Pruebas) {
    ctest --test-dir $salida --output-on-failure
    Comprobar 'pruebas'
}

Write-Host "Listo: $salida\almalili.exe"
