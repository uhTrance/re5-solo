# Watches RE5 while it runs: shows fault lines from re5_solo.log as they happen, and when the game closes saves a
# copy of the log (the next launch overwrites it) to crashlogs\ beside this repo, with any new dump listed.
$out = Join-Path (Split-Path $PSScriptRoot) 'crashlogs'
New-Item -ItemType Directory -Force $out | Out-Null

$proc = Get-Process re5dx9 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Host 'Waiting for RE5 to start...' -ForegroundColor Yellow }
while (-not $proc) { Start-Sleep -Seconds 1; $proc = Get-Process re5dx9 -ErrorAction SilentlyContinue | Select-Object -First 1 }
Start-Sleep -Seconds 3  # the loader recreates the log at launch
$game = Split-Path $proc.Path  # the game folder, from the running exe
$log = Join-Path $game 're5_solo.log'
for ($i = 0; $i -lt 30 -and -not (Test-Path $log); $i++) { Start-Sleep -Seconds 1 }
if (-not (Test-Path $log)) { Write-Host "No re5_solo.log in $game (is the mod installed?)" -ForegroundColor Red; exit 1 }
Write-Host "Watching RE5 (pid $($proc.Id)). Fault lines show here; the log is saved when the game closes." -ForegroundColor Cyan
$started = Get-Date

$stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite,Delete')
$reader = New-Object IO.StreamReader($stream)
$reader.BaseStream.Seek(0, 'End') | Out-Null
$context = 0
while (-not $proc.HasExited) {
    $line = $reader.ReadLine()
    if ($null -eq $line) { Start-Sleep -Milliseconds 200; continue }
    if ($line -match 'UNHANDLED|FAULT|minidump') {
        Write-Host $line -ForegroundColor Red; $context = 3
    } elseif ($context -gt 0) {
        Write-Host $line -ForegroundColor DarkYellow; $context--
    }
}
Start-Sleep -Seconds 1
$rest = $reader.ReadToEnd()
$reader.Close()
foreach ($line in $rest -split "`r?`n") { if ($line -match 'UNHANDLED|FAULT|minidump|callers|eax') { Write-Host $line -ForegroundColor Red } }

$stamp = Get-Date -Format 'yyyy-MM-dd_HHmmss'
$copy = Join-Path $out "re5_solo_$stamp.log"
Copy-Item $log $copy
Write-Host "RE5 closed at $(Get-Date -Format HH:mm:ss) (exit code $($proc.ExitCode)). Log saved: $copy" -ForegroundColor Cyan
Get-ChildItem $game -Filter 're5_solo_*.dmp' | Where-Object { $_.LastWriteTime -gt $started } |
    ForEach-Object { Write-Host "New dump: $($_.FullName)" -ForegroundColor Red }
Write-Host '--- last 15 log lines ---'
Get-Content $copy -Tail 15
