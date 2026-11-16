$ErrorActionPreference = 'Stop'
trap {
    $message = ($_ | Out-String).Replace('%', '%25').Replace("`r", ' ').Replace("`n", ' ')
    Write-Output "::error file=desktop/tests/windows_package.ps1::$message"
    exit 1
}
$work = Join-Path $env:RUNNER_TEMP 'LuaC Java Studio package test'
if (Test-Path $work) { Remove-Item -Force -Recurse $work }
Expand-Archive -LiteralPath 'dist/LuaC-Studio-Java-windows-x86_64.zip' -DestinationPath $work
$appDir = Join-Path $work 'LuaC-Studio'
foreach ($name in @('LuaC-Studio.exe', 'app/LuaC-Studio.jar', 'runtime/bin/java.exe', 'luac.exe', 'tcc/tcc.exe', 'gcc/bin/gcc.exe', 'runtime.h')) {
    if (!(Test-Path (Join-Path $appDir $name))) { throw "Missing package file: $name" }
}
# No system Java, MSYS2 or GCC can be found in this PATH.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:JAVA_HOME = ''
$env:CC = ''
$jar = Join-Path $appDir 'app/LuaC-Studio.jar'
$java = Join-Path $appDir 'runtime/bin/java.exe'
$classPath = "$jar;$((Resolve-Path build/java-test).Path)"
$env:LUAC_TRACE = '1'
$results = & $java '-Dfile.encoding=UTF-8' -cp $classPath com.luac.desktop.DesktopTest $appDir '--gui' 2>&1
$exitCode = $LASTEXITCODE
$results | ForEach-Object { Write-Output "$_" }
if ($exitCode -ne 0) { throw "Packaged GUI/native compiler test failed: $($results -join ' ')" }
# Also test the actual double-clickable launcher, independently of system Java.
$proc = Start-Process -FilePath (Join-Path $appDir 'LuaC-Studio.exe') -PassThru
try {
    $deadline = (Get-Date).AddSeconds(30)
    do { Start-Sleep -Milliseconds 500; $proc.Refresh() } while (!$proc.HasExited -and $proc.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline)
    if ($proc.HasExited -or $proc.MainWindowHandle -eq 0) { throw 'Packaged Java launcher did not open a window' }
    Write-Output 'Extracted Java launcher window and bundled GCC: OK'
} finally { if (!$proc.HasExited) { $proc.Kill(); $proc.WaitForExit() } }
