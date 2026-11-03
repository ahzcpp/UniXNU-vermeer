#requires -Version 7.0
#requires -PSEdition Core
<#
.SYNOPSIS
Capture read-only UniXNU UEFI fastboot queries and their original output.
.DESCRIPTION
Only VID 18d1 / PID d00d / serial K70UEFI20261104 is accepted. The executable is
fixed to C:\Windows\System32\fastboot.exe. This sends devices and four getvar
queries only; it cannot flash, erase, boot, reboot, download, or select a slot.
Each process has a deadline and separate stdout/stderr files. Fastboot normally
writes successful getvar responses to stderr; both streams are examined.
Inventory and small queries have 5-second deadlines; the log query has 45
seconds. Each stream is hard-capped at 1 MiB. Termination/drain cleanup uses
bounded waits of at most two additional seconds per process.

Dot-sourcing defines offline-testable functions without enumerating USB or
starting processes. Windows driver compatibility still needs a handset test.
.EXAMPLE
pwsh -File .\scripts\capture-k70-uefi.ps1
.EXAMPLE
pwsh -File .\scripts\capture-k70-uefi.ps1 -OutputRoot E:\K70-captures\uefi
#>
[CmdletBinding()]
param([string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\captures\k70-uefi'))

Set-StrictMode -Version Latest

function Initialize-K70UefiOutputCopy {
    if ('K70Uefi.BoundedOutput' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
namespace K70Uefi {
    public static class BoundedOutput {
        public static async Task<bool> Copy(Stream input, Stream output, int limit, CancellationToken token) {
            var buffer = new byte[16384];
            int total = 0;
            while (true) {
                int read = await input.ReadAsync(buffer, 0, buffer.Length, token).ConfigureAwait(false);
                if (read == 0) return true;
                int keep = Math.Min(read, limit - total);
                if (keep > 0) await output.WriteAsync(buffer, 0, keep, token).ConfigureAwait(false);
                total += keep;
                if (read != keep) return false;
            }
        }
    }
}
'@ -ErrorAction Stop | Out-Null
}

function Invoke-K70UefiProcess {
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][ValidatePattern('^[a-z0-9-]+$')][string]$Label,
        [ValidateRange(50,60000)][int]$TimeoutMs = 5000,
        [ValidateRange(1024,4194304)][int]$MaxStreamBytes = 1MB
    )
    Initialize-K70UefiOutputCopy
    $stdoutPath = Join-Path $Directory ($Label + '.stdout.txt')
    $stderrPath = Join-Path $Directory ($Label + '.stderr.txt')
    $result = [ordered]@{
        executable=$Executable; arguments=$Arguments; timeout_ms=$TimeoutMs
        exit_code=$null; timeout=$false; output_limit=$false; output_complete=$false
        elapsed_ms=0; stdout_path=$stdoutPath; stderr_path=$stderrPath; error=$null
    }
    $process = [Diagnostics.Process]::new()
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Executable
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process.StartInfo = $info
    $stdoutFile = $null; $stderrFile = $null
    $stdoutCopy = $null; $stderrCopy = $null
    $cancelCopy = [Threading.CancellationTokenSource]::new()
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        $stdoutFile = [IO.File]::Open($stdoutPath, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $stderrFile = [IO.File]::Open($stderrPath, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::Read)
        [void]$process.Start()
        $stdoutCopy = [K70Uefi.BoundedOutput]::Copy($process.StandardOutput.BaseStream, $stdoutFile, $MaxStreamBytes, $cancelCopy.Token)
        $stderrCopy = [K70Uefi.BoundedOutput]::Copy($process.StandardError.BaseStream, $stderrFile, $MaxStreamBytes, $cancelCopy.Token)
        while (-not $process.WaitForExit(20)) {
            if ($clock.ElapsedMilliseconds -ge $TimeoutMs) {
                $result.timeout = $true; break
            }
            if (($stdoutCopy.IsCompletedSuccessfully -and -not $stdoutCopy.Result) -or
                ($stderrCopy.IsCompletedSuccessfully -and -not $stderrCopy.Result)) {
                $result.output_limit = $true; break
            }
        }
        if ($result.timeout -or $result.output_limit) {
            try { $process.Kill($true) } catch { $result.error = $_.Exception.Message }
        }
        # Never use an unbounded WaitForExit/ReadToEnd, even during cleanup.
        if (-not $process.WaitForExit(1000)) {
            $result.error = 'Process did not terminate within cleanup deadline.'
        } else {
            $result.exit_code = $process.ExitCode
        }
        $copies = [Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($stdoutCopy,$stderrCopy))
        try { $result.output_complete = $copies.Wait(1000) }
        catch { $result.error = $_.Exception.Message }
        if (-not $result.output_complete) {
            $cancelCopy.Cancel()
            $process.StandardOutput.Close(); $process.StandardError.Close()
        }
        if (($stdoutCopy.IsCompletedSuccessfully -and -not $stdoutCopy.Result) -or
            ($stderrCopy.IsCompletedSuccessfully -and -not $stderrCopy.Result)) {
            $result.output_limit = $true
            $result.output_complete = $false
        }
    } catch {
        $result.error = $_.Exception.Message
        try { if (-not $process.HasExited) { $process.Kill($true); [void]$process.WaitForExit(1000) } } catch { }
    } finally {
        # Stream output is persisted even for timeout, FAIL, or start error.
        $cancelCopy.Cancel()
        if ($null -ne $stdoutFile) { $stdoutFile.Dispose() }
        if ($null -ne $stderrFile) { $stderrFile.Dispose() }
        $process.Dispose()
        $cancelCopy.Dispose()
        $result.elapsed_ms = $clock.ElapsedMilliseconds
        $clock.Stop()
    }
    return [pscustomobject]$result
}

function Test-K70UefiProcessSuccess {
    param([Parameter(Mandatory)]$Result)
    return (-not $Result.timeout -and -not $Result.output_limit -and
        $Result.output_complete -and $null -eq $Result.error -and $Result.exit_code -eq 0)
}

function Read-K70UefiOutput {
    param([Parameter(Mandatory)]$Result)
    # This combined string is for response validation only. Original channels
    # remain in separate files; it does not claim inter-stream chronological order.
    return [IO.File]::ReadAllText($Result.stdout_path) + "`n" + [IO.File]::ReadAllText($Result.stderr_path)
}

function Assert-K70UefiArguments {
    param([Parameter(Mandatory)][string[]]$Arguments)
    if ($Arguments.Count -eq 1 -and $Arguments[0] -ceq 'devices') { return }
    if ($Arguments.Count -eq 4 -and $Arguments[0] -ceq '-s' -and
        $Arguments[1] -ceq 'K70UEFI20261104' -and $Arguments[2] -ceq 'getvar' -and
        @('product','version','k70-hello','k70-log') -ccontains $Arguments[3]) { return }
    throw 'Only devices and the four fixed K70 UEFI getvar queries are permitted.'
}

function Invoke-K70UefiFastboot {
    param(
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][string]$Label,
        [int]$TimeoutMs = 5000,
        # Function-level injection is solely for dot-sourced offline mocks.
        # The script entry point always uses the fixed System32 executable.
        [string]$Executable = 'C:\Windows\System32\fastboot.exe',
        [string[]]$PrefixArguments = @()
    )
    Assert-K70UefiArguments -Arguments $Arguments
    return Invoke-K70UefiProcess -Executable $Executable -Arguments @($PrefixArguments + $Arguments) `
        -Directory $Directory -Label $Label -TimeoutMs $TimeoutMs
}

function Get-K70UefiIdentity {
    param([Parameter(Mandatory)][AllowEmptyCollection()][object[]]$PresentDevices)
    # Inventory contains present devices only. Require the physical USB node,
    # whose exact serial is preserved even when a composite interface has a
    # generated instance suffix. An MI_XX child alone does not prove identity.
    $matches = @($PresentDevices | Where-Object {
        $_.InstanceId -cmatch '^USB\\(?i:VID_18D1&PID_D00D)(?:&(?i:REV_[0-9A-F]{4}))?\\K70UEFI20261104$'
    })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one present physical USB device with VID 18d1 / PID d00d / serial K70UEFI20261104; found $($matches.Count)."
    }
    return $matches[0]
}

function Test-K70UefiFastbootIdentity {
    param([Parameter(Mandatory)]$Result)
    if (-not (Test-K70UefiProcessSuccess $Result)) { return $false }
    $lines = @((Read-K70UefiOutput $Result) -split '\r?\n' | Where-Object {
        $_ -cmatch '^K70UEFI20261104\s+fastboot\s*$'
    })
    return $lines.Count -eq 1
}

function Test-K70UefiVariable {
    param([Parameter(Mandatory)]$Result, [Parameter(Mandatory)][string]$Name,
          [Parameter(Mandatory)][string]$Expected)
    if (-not (Test-K70UefiProcessSuccess $Result)) { return $false }
    # Classic and recent platform-tools use either "name:" or
    # "(bootloader) name:". An unrelated prose substring does not qualify.
    $pattern = '(?m)^(?:\(bootloader\)[ \t]*)?' + [regex]::Escape($Name) +
        ':[ \t]*' + [regex]::Escape($Expected) + '[ \t]*\r?$'
    return (Read-K70UefiOutput $Result) -cmatch $pattern
}

function Invoke-K70UefiIdentityCheck {
    param([Parameter(Mandatory)][string]$Directory, [Parameter(Mandatory)][string]$Label,
          [Parameter(Mandatory)][AllowEmptyCollection()][Collections.Generic.List[object]]$Commands)
    # Run in a bounded process. Require the USB identity again before each
    # query, while every query also explicitly selects the fixed serial.
    $inventoryCode = @'
$ErrorActionPreference = 'Stop'
$items = @(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object {
    $_.InstanceId -cmatch '^USB\\(?i:VID_18D1&PID_D00D)(?:&(?i:REV_[0-9A-F]{4}))?\\K70UEFI20261104$'
} | Select-Object Class,FriendlyName,Status,InstanceId)
ConvertTo-Json -InputObject $items -Depth 4 -Compress
'@
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($inventoryCode))
    $shell = (Get-Process -Id $PID).Path
    $inventory = Invoke-K70UefiProcess -Executable $shell `
        -Arguments @('-NoProfile','-NonInteractive','-EncodedCommand',$encoded) `
        -Directory $Directory -Label $Label -TimeoutMs 5000
    $Commands.Add($inventory)
    if (-not (Test-K70UefiProcessSuccess $inventory)) {
        throw "Bounded USB identity check failed; see $Label stderr and capture.json."
    }
    $present = @([IO.File]::ReadAllText($inventory.stdout_path) | ConvertFrom-Json)
    return Get-K70UefiIdentity -PresentDevices $present
}

function Invoke-K70UefiCapture {
    param([Parameter(Mandatory)][string]$Root)
    if (-not $IsWindows) { throw 'This collector requires Windows and PowerShell 7.' }
    $rootPath = [IO.Path]::GetFullPath($Root)
    [void][IO.Directory]::CreateDirectory($rootPath)
    $directory = Join-Path $rootPath ([DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($directory)
    $commands = [Collections.Generic.List[object]]::new()
    $report = [ordered]@{
        created_utc=[DateTime]::UtcNow.ToString('o'); read_only=$true
        serial='K70UEFI20261104'; vid='18d1'; pid='d00d'
        fastboot='C:\Windows\System32\fastboot.exe'; usb_identity=$null
        identity_verified=$false; fastboot_responding=$false; hello_verified=$false
        log_query_completed=$false; firmware_log_received=$false
        log_capture=$null; status='failed'; error=$null
        limitation='PC driver binding, provider behavior, and live USB log retrieval require handset validation. This captures only the current UEFI session.'
        commands=$commands
    }
    try {
        $report.usb_identity = Invoke-K70UefiIdentityCheck -Directory $directory -Label '00-usb-inventory' -Commands $commands
        $report.identity_verified = $true
        $devices = Invoke-K70UefiFastboot -Arguments @('devices') -Directory $directory -Label '01-devices'
        $commands.Add($devices)
        if (-not (Test-K70UefiFastbootIdentity $devices)) {
            throw 'The exact diagnostic serial was not listed by fastboot. Check the Windows Android Bootloader/WinUSB driver and the UEFI USB provider.'
        }
        $report.fastboot_responding = $true
        $null = Invoke-K70UefiIdentityCheck -Directory $directory -Label '02-usb-check' -Commands $commands
        $product = Invoke-K70UefiFastboot -Arguments @('-s','K70UEFI20261104','getvar','product') `
            -Directory $directory -Label '02-product'
        $commands.Add($product)
        if (-not (Test-K70UefiVariable $product 'product' 'vermeer')) { throw 'UEFI product query did not return vermeer.' }
        $null = Invoke-K70UefiIdentityCheck -Directory $directory -Label '03-usb-check' -Commands $commands
        $version = Invoke-K70UefiFastboot -Arguments @('-s','K70UEFI20261104','getvar','version') `
            -Directory $directory -Label '03-version'
        $commands.Add($version)
        if (-not (Test-K70UefiVariable $version 'version' '0.4')) { throw 'UEFI version query did not return 0.4.' }
        $null = Invoke-K70UefiIdentityCheck -Directory $directory -Label '04-usb-check' -Commands $commands
        $hello = Invoke-K70UefiFastboot -Arguments @('-s','K70UEFI20261104','getvar','k70-hello') `
            -Directory $directory -Label '04-hello'
        $commands.Add($hello)
        if (-not (Test-K70UefiVariable $hello 'k70-hello' 'HELLO K70 UEFI read-only query service')) {
            throw 'The diagnostic HELLO response was not verified; log collection was not attempted.'
        }
        $report.hello_verified = $true
        $null = Invoke-K70UefiIdentityCheck -Directory $directory -Label '05-usb-check' -Commands $commands
        $log = Invoke-K70UefiFastboot -Arguments @('-s','K70UEFI20261104','getvar','k70-log') `
            -Directory $directory -Label '05-log' -TimeoutMs 45000
        $commands.Add($log)
        if (-not (Test-K70UefiProcessSuccess $log)) { throw 'Firmware log query failed or exceeded its deadline; partial streams are retained.' }
        # Fastboot's exit status establishes protocol completion; log content
        # itself can contain words such as FAIL and is never parsed as status.
        if (([IO.FileInfo]$log.stdout_path).Length + ([IO.FileInfo]$log.stderr_path).Length -eq 0) {
            throw 'The successful log process produced no output; no log receipt is claimed.'
        }
        $report.log_query_completed = $true
        # Completion output alone can be an empty ring. Claim payload receipt
        # only when the classic/current fastboot INFO decoration is present.
        $report.firmware_log_received = (Read-K70UefiOutput $log) -cmatch '(?m)^\(bootloader\) .+'
        $report.log_capture = [ordered]@{
            stdout=$log.stdout_path; stderr=$log.stderr_path
            format='Raw fastboot output, including INFO decoration and completion/timing text; not a byte-exact ring export.'
            stdout_sha256=(Get-FileHash -LiteralPath $log.stdout_path -Algorithm SHA256).Hash.ToLowerInvariant()
            stderr_sha256=(Get-FileHash -LiteralPath $log.stderr_path -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        $report.status = 'captured'
    } catch {
        $report.error = $_.Exception.Message
    } finally {
        $reportPath = Join-Path $directory 'capture.json'
        [IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
    }
    return [pscustomobject]@{
        status=$report.status; report=$reportPath; directory=$directory
        identity_verified=$report.identity_verified; fastboot_responding=$report.fastboot_responding
        hello_verified=$report.hello_verified; log_query_completed=$report.log_query_completed
        firmware_log_received=$report.firmware_log_received; error=$report.error
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    try {
        $capture = Invoke-K70UefiCapture -Root $OutputRoot
        $capture | ConvertTo-Json -Depth 4
        if ($capture.status -ne 'captured') { exit 1 }
    } catch {
        Write-Error -Message ('K70 UEFI capture failed: ' + $_.Exception.Message) -ErrorAction Continue
        exit 1
    }
}
