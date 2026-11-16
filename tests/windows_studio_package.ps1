$ErrorActionPreference = 'Stop'
# Prevent a missing-DLL dialog from blocking an unattended CI runner.
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class PackageErrorMode {
    [DllImport("kernel32.dll")]
    public static extern uint SetErrorMode(uint mode);
}
'@
[void][PackageErrorMode]::SetErrorMode(0x8003)

$work = Join-Path $env:RUNNER_TEMP 'LuaC Studio extracted test'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
Expand-Archive -LiteralPath 'dist/LuaC-Studio-windows-x86_64.zip' -DestinationPath $work
$gui = @(Get-ChildItem -Path $work -Filter 'luac-gui.exe' -Recurse -File)
if ($gui.Count -ne 1) { throw "Expected exactly one packaged Studio executable" }
$folder = $gui[0].DirectoryName
foreach ($name in @('glfw3.dll', 'libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll', 'luac.exe', 'build/shader.vert.spv', 'build/shader.frag.spv')) {
    if (!(Test-Path (Join-Path $folder $name))) { throw "Missing packaged dependency: $name" }
}
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
function Test-Executable([string] $program, [string] $argument, [string] $expected) {
    $stdout = Join-Path $work 'stdout.txt'
    $stderr = Join-Path $work 'stderr.txt'
    $process = Start-Process -FilePath $program -ArgumentList $argument -WorkingDirectory $folder -NoNewWindow -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (!$process.WaitForExit(15000)) {
        $process.Kill()
        throw "Executable did not finish: $program"
    }
    if ($process.ExitCode -ne 0 -or !(Select-String -Path $stdout -SimpleMatch $expected -Quiet)) {
        Get-Content $stdout, $stderr -ErrorAction SilentlyContinue
        throw "Package startup failed: $program (exit $($process.ExitCode))"
    }
}
Test-Executable $gui[0].FullName '--check-dependencies' 'LuaC Studio native dependencies loaded'
Test-Executable (Join-Path $folder 'luac.exe') '--version' 'LuaC'
Write-Output 'Extracted Windows Studio dependencies: OK'
