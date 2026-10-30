# Packages a PRISM_RELEASE build for Windows and checks the result:
#
#   packaging/windows/package.ps1 <build-dir> <out-dir>
#
# Writes <out-dir>/prism-<version>-windows-x64.zip (a folder holding prism_gui.exe, prism.exe,
# LICENSE.md, THIRD_PARTY_NOTICES.txt, README.txt and licenses\LibRaw\) and
# <out-dir>/LibRaw-<version>-source.tar.gz, the exact tarball the build compiled.
param(
    [Parameter(Mandatory)] [string] $Build,
    [Parameter(Mandatory)] [string] $Out
)
$ErrorActionPreference = 'Stop'

# PRISM_VERSION, LIBRAW_VERSION, LIBRAW_SHA256, LIBRAW_TARBALL
$info = @{}
foreach ($line in Get-Content (Join-Path $Build 'release.env')) {
    if ($line -match '^([A-Z0-9_]+)=(.*)$') { $info[$Matches[1]] = $Matches[2] }
}
$name = "prism-$($info.PRISM_VERSION)-windows-x64"
$stage = Join-Path $Out "stage\$name"

Remove-Item -Recurse -Force (Join-Path $Out 'stage') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Out | Out-Null
cmake --install $Build --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'cmake --install failed' }

Write-Output '== Checks'
# Only Windows' own DLLs: LibRaw and the C runtime are linked statically (/MT), so no
# libraw_r.dll and no Visual C++ redistributable.
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$dumpbin = (Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" | Select-Object -First 1).FullName
foreach ($exe in 'prism_gui.exe', 'prism.exe') {
    $path = Join-Path $stage $exe
    $dlls = & $dumpbin /dependents $path | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() }
    Write-Output "${exe}: $($dlls -join ', ')"
    foreach ($dll in $dlls) {
        $system = Test-Path (Join-Path "$env:SystemRoot\System32" $dll)
        if ($dll -match '^(libraw|vcruntime|msvcp|ucrtbase|api-ms-win-crt)' -or -not $system) {
            throw "$exe depends on $dll, which isn't part of Windows"
        }
    }
    $version = (Get-Item $path).VersionInfo.ProductVersion
    if ($version -ne $info.PRISM_VERSION) { throw "$exe has version '$version', not $($info.PRISM_VERSION)" }
}
& (Join-Path $stage 'prism.exe') --version
if ($LASTEXITCODE -ne 0) { throw 'prism.exe --version failed' }

# Prism's licence, with its Required Notice, and the README that points to it.
foreach ($f in 'LICENSE.md', 'THIRD_PARTY_NOTICES.txt', 'README.txt') {
    if (-not (Test-Path (Join-Path $stage $f) -PathType Leaf)) { throw "the download has no $f" }
}
if (-not (Select-String -Quiet -Pattern '^Required Notice: ' -Path (Join-Path $stage 'LICENSE.md'))) {
    throw "LICENSE.md has no 'Required Notice:' line"
}
if (-not (Select-String -Quiet -SimpleMatch 'LICENSE.md' -Path (Join-Path $stage 'README.txt'))) {
    throw "README.txt doesn't refer to LICENSE.md"
}

# LibRaw's source: the tarball the build unpacked, with the pinned hash, and its licence files
# in the download exactly as they are in it.
$hash = (Get-FileHash -Algorithm SHA256 $info.LIBRAW_TARBALL).Hash.ToLower()
if ($hash -ne $info.LIBRAW_SHA256) { throw "$($info.LIBRAW_TARBALL) has SHA-256 $hash, not the pinned $($info.LIBRAW_SHA256)" }
$unpacked = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory $unpacked | Out-Null
tar -xzf $info.LIBRAW_TARBALL -C $unpacked
foreach ($f in 'LICENSE.CDDL', 'COPYRIGHT') {
    $a = (Get-FileHash (Join-Path $unpacked "LibRaw-$($info.LIBRAW_VERSION)\$f")).Hash
    $b = (Get-FileHash (Join-Path $stage "licenses\LibRaw\$f")).Hash
    if ($a -ne $b) { throw "licenses\LibRaw\$f differs from LibRaw's source" }
}
Remove-Item -Recurse -Force $unpacked
Copy-Item $info.LIBRAW_TARBALL (Join-Path $Out "LibRaw-$($info.LIBRAW_VERSION)-source.tar.gz")
Write-Output "LibRaw $($info.LIBRAW_VERSION) source: SHA-256 $hash (matches the pin); licence files identical"

$zip = Join-Path $Out "$name.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $zip
Remove-Item -Recurse -Force (Join-Path $Out 'stage')
Write-Output "== Wrote $zip"
