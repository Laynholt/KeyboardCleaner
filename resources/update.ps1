param(
    [Parameter(Mandatory=$true)][int]$ParentPid,
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Target,
    [Parameter(Mandatory=$true)][string]$Backup,
    [Parameter(Mandatory=$true)][string]$Sha256
)
$ErrorActionPreference = 'Stop'
$replaced = $false
$healthy = $false
$restored = $false
$nextApp = $null
try {
    $parent = Get-Process -Id $ParentPid -ErrorAction SilentlyContinue
    if ($parent -and !$parent.WaitForExit(30000)) { exit 1 }
    if ($Sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Invalid checksum' }
    $hashStream = [System.IO.File]::OpenRead($Source)
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        $actualHash = [System.BitConverter]::ToString($hasher.ComputeHash($hashStream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $hashStream.Dispose()
        $hasher.Dispose()
    }
    if ($actualHash -cne $Sha256) {
        throw 'Checksum mismatch'
    }
    [System.IO.File]::Replace($Source, $Target, $Backup)
    $replaced = $true
    $nextApp = Start-Process -FilePath $Target -WorkingDirectory (Split-Path -Parent $Target) -WindowStyle Normal -PassThru
    if (!$nextApp.WaitForInputIdle(10000)) { throw 'New application did not initialize' }
    Start-Sleep -Milliseconds 1000
    $nextApp.Refresh()
    if ($nextApp.HasExited) { throw 'New application exited during startup' }
    $healthy = $true
} catch {
    if ($replaced) {
        try {
            if ($nextApp) {
                $nextApp.Refresh()
                if (!$nextApp.HasExited) {
                    Stop-Process -Id $nextApp.Id -Force
                    $nextApp.WaitForExit(10000) | Out-Null
                }
            }
            [System.IO.File]::Replace($Backup, $Target, (Join-Path (Split-Path -Parent $Source) 'failed.exe'))
            $restored = $true
        } catch {
            # Preserve the backup if rollback fails; it remains a runnable EXE.
            if (Test-Path -LiteralPath $Backup) {
                Start-Process -FilePath $Backup -WindowStyle Normal
            }
        }
    }
    if (!$replaced -or $restored) {
        Start-Process -FilePath $Target -WorkingDirectory (Split-Path -Parent $Target) -ArgumentList '--update-failed' -WindowStyle Normal
    }
} finally {
    if ($healthy -or $restored -or !$replaced) {
        foreach ($file in @($Source, $Backup, (Join-Path (Split-Path -Parent $Source) 'failed.exe'))) {
            Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
        }
        $scriptPath = $MyInvocation.MyCommand.Path
        if ((Split-Path -Leaf $scriptPath) -eq 'apply.ps1' -and
            (Split-Path -Parent $scriptPath) -eq (Split-Path -Parent $Source)) {
            Remove-Item -LiteralPath $scriptPath -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath (Split-Path -Parent $scriptPath) -ErrorAction SilentlyContinue
        }
    }
}
