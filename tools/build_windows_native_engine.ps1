param(
  [ValidateSet('debug','profile','release')][string]$Mode = 'debug',
  [string]$Version = '3.41.10-ohos-0.0.2-beta.operit.1',
  [string]$Python = 'python',
  [int]$Jobs = 8,
  [switch]$RunTests,
  [string]$Ninja
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$env:GYP_MSVS_VERSION = '2022'
$env:vs2022_install = & $vswhere -latest -version '[17.0,18.0)' -property installationPath
if (!$env:vs2022_install) { throw 'Visual Studio 2022 with C++ tools is required.' }
$env:GYP_MSVS_OVERRIDE_PATH = $env:vs2022_install
if (!$Ninja) {
  $Ninja = Join-Path $env:vs2022_install 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
}
$env:WINDOWSSDKDIR = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
Push-Location $root
try {
  & $Python engine/src/flutter/tools/gn --runtime-mode $Mode --no-goma
  if ($LASTEXITCODE) { throw 'Engine GN generation failed.' }
  $out = Join-Path $root "engine/src/out/host_$Mode"
  $targets = @('flutter/shell/platform/windows:windows', 'gen_snapshot')
  if ($RunTests) { $targets += 'flutter_windows_unittests' }
  & $Ninja -C $out -j $Jobs @targets
  if ($LASTEXITCODE) { throw 'Engine build failed.' }
  if ($RunTests) {
    & "$out/flutter_windows_unittests.exe" '--gtest_filter=NativeCompositionTest.*:PlatformViewPluginTest.*:FlutterWindowsEngineTest.RegisterPlatformViewBeforeRun:CompositorOpenGLTest.*'
    if ($LASTEXITCODE) { throw 'Windows engine tests failed.' }
  }
  $stage = Join-Path $out "native-artifact/windows-x64-$Mode-$Version-$([guid]::NewGuid().ToString('N'))"
  New-Item -ItemType Directory -Force -Path $stage | Out-Null
  foreach ($file in @('flutter_windows.dll','flutter_windows.dll.exp','flutter_windows.dll.pdb','flutter_windows.dll.lib','flutter_windows.h', 'flutter_export.h','flutter_macros.h','flutter_messenger.h','flutter_plugin_registrar.h','flutter_texture_registrar.h','icudtl.dat','gen_snapshot.exe')) {
    $source = Join-Path $out $file
    if (!(Test-Path -LiteralPath $source)) { throw "Missing engine artifact: $source" }
    Copy-Item -LiteralPath $source -Destination $stage
  }
  Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
  Copy-Item -LiteralPath (Join-Path $out 'cpp_client_wrapper') -Destination $stage -Recurse -Force
  $files = Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName
  $hashes = [ordered]@{}
  foreach ($file in $files) {
    $relative = $file.FullName.Substring($stage.Length + 1).Replace('\','/')
    $hashes[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  $manifest = [ordered]@{
    frameworkVersion = $Version
    frameworkRevision = (& git rev-parse HEAD).Trim()
    engineRevision = (Get-Content bin/internal/engine.version -Raw).Trim()
    target = 'windows-x64'
    runtimeMode = $Mode
    files = $hashes
  }
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'engine-release.json') -Encoding utf8
  $archive = Join-Path $out "windows-x64-$Mode.zip"
  Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
  Write-Output "Engine artifact: $archive"
} finally {
  Pop-Location
}
