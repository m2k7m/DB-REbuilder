param(
    [switch]$CleanSdk
)

$ErrorActionPreference = 'Stop'

$repo = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$drive = $repo.Substring(0, 1).ToLowerInvariant()
$srcPath = "/mnt/$drive" + ($repo.Substring(2) -replace '\\', '/')

if ($CleanSdk) {
    wsl.exe -- bash -c 'rm -rf ~/ps4-payload-sdk ~/ps4-payload-sdk-src'
    Write-Host "[wsl-build] SDK removed - it will be re-bootstrapped on this run"
}

$bashScript = @'
set -euo pipefail

SRC="__REPO__"
DST="$HOME/build/DB-REbuilder"
SDK_SRC="$HOME/ps4-payload-sdk-src"
SDK="$HOME/ps4-payload-sdk"

if [ ! -f "$SDK/toolchain/orbis.mk" ]; then
    echo "[wsl-build] ps4-payload-sdk not installed, bootstrapping (first run only)"
    if [ ! -d "$SDK_SRC/.git" ]; then
        git clone --depth 1 https://github.com/ps4-payload-dev/sdk "$SDK_SRC"
    fi
    make -C "$SDK_SRC" DESTDIR="$SDK" clean install
fi

echo "[wsl-build] syncing sources to $DST"
rm -rf "$DST"
mkdir -p "$DST/build"
cp -r "$SRC/src" "$DST/src"
cp "$SRC/Makefile" "$DST/Makefile"
find "$DST" -type f \( -name '*.c' -o -name '*.h' -o -name '*.x' -o -name 'Makefile' \) -exec sed -i 's/\r$//' {} +

echo "[wsl-build] building..."
export PS4_PAYLOAD_SDK="$SDK"
make -C "$DST" clean all

cp "$DST"/db-rebuilder-v*.elf "$SRC"/
cp "$DST"/db-rebuilder-v*.bin "$SRC"/

echo "[wsl-build] done, artifacts:"
ls -lh "$SRC"/db-rebuilder-v*
'@

$bashScript = $bashScript.Replace('__REPO__', $srcPath)
$b64 = [Convert]::ToBase64String([System.Text.Encoding]::UTF8.GetBytes($bashScript))

wsl.exe -- bash -c "echo $b64 | base64 -d | bash"
exit $LASTEXITCODE
