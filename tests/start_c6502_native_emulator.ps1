param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [ValidatePattern('^[a-z0-9-]+$')][string]$Name = 'native-final',
    [string]$BaseNand,
    [string]$OutputRoot,
    [int]$QmpPort = 6172,
    [int]$GdbPort = 6173,
    [int]$VncDisplay = 20,
    [ValidatePattern('^[^<>:"/\\|?*]+\.exe$')][string]$InstalledName = 'GAM4980.exe'
)
$ErrorActionPreference = 'Stop'
$nativeRoot = Split-Path -Parent $PSScriptRoot
$nativeEmulator = 'D:/Downloads/bbk9288-emulator-v9288-0.1.2/emulator/bbk9288-emulator-v9288-0.1.2-windows-x64'
$nativePython = 'D:/ProgramData/miniconda3/python.exe'
if ($OutputRoot) {
    $nativeOutput = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) $Name
} else {
    $nativeOutput = Join-Path $nativeRoot "build/$Name"
}
$nativeSource = (Resolve-Path -LiteralPath $Exe).Path
if ($BaseNand) {
    $nativeBase = (Resolve-Path -LiteralPath $BaseNand).Path
} else {
    $nativeBase = Join-Path $nativeRoot 'build/c6502-native-test.raw'
}
foreach ($port in @($QmpPort, $GdbPort, (5900 + $VncDisplay))) {
    if (Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) {
        throw "Port $port is already in use; the running emulator is unchanged."
    }
}
$nativeStage = Join-Path $nativeOutput 'stage/系统/程序'
New-Item -ItemType Directory -Path $nativeStage -Force | Out-Null
Copy-Item -LiteralPath $nativeSource -Destination (Join-Path $nativeStage $InstalledName)
$nativeNand = Join-Path $nativeOutput 'nand.raw'
& $nativePython (Join-Path $PSScriptRoot 'prepare_emulator_nand.py') `
    --image-tool "$nativeEmulator/scripts/bbk9288s_nand_image.py" `
    --base $nativeBase `
    --source "$nativeOutput/stage" --output $nativeNand --flat "$nativeOutput/nand.flat"
if ($LASTEXITCODE -ne 0) { throw 'NAND preparation failed' }
$nativeNand = $nativeNand.Replace('\', '/')
$nativeArgs = @('-L', 'share', '-name', $Name,
    '-machine', "bbk9288,nand-image=$nativeNand,usb-connected=on",
    '-cpu', 'c33l05,exit-on-halt=off', '-rtc', 'base=localtime',
    '-display', "vnc=127.0.0.1:$VncDisplay",
    '-qmp', "tcp:127.0.0.1:$QmpPort,server=on,wait=off",
    '-gdb', "tcp:127.0.0.1:$GdbPort", '-serial', 'none', '-monitor', 'none')
$nativeProcess = Start-Process -FilePath "$nativeEmulator/qemu-system-s1c33-hsdma-p7.exe" `
    -WorkingDirectory $nativeEmulator -WindowStyle Hidden -ArgumentList $nativeArgs `
    -RedirectStandardError "$nativeOutput/qemu.err" `
    -RedirectStandardOutput "$nativeOutput/qemu.out" -PassThru
[pscustomobject]@{
    Pid = $nativeProcess.Id
    QmpPort = $QmpPort
    Exe = $nativeSource
    Sha256 = (Get-FileHash -LiteralPath $nativeSource -Algorithm SHA256).Hash
    Output = $nativeOutput
} | ConvertTo-Json
