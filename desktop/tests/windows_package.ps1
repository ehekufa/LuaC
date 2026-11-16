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
New-Item -ItemType Directory -Force build/ui-screenshots | Out-Null
$env:LUAC_UI_SCREENSHOTS = (Resolve-Path build/ui-screenshots).Path
$results = & $java '-Dfile.encoding=UTF-8' -cp $classPath com.luac.desktop.DesktopTest $appDir '--gui' 2>&1
$exitCode = $LASTEXITCODE
$results | ForEach-Object { Write-Output "$_" }
if ($exitCode -ne 0) { throw "Packaged GUI/native compiler test failed: $($results -join ' ')" }
# AOT exercises the standalone linker path (different startup objects from JIT).
$env:CPATH = Join-Path $appDir 'gcc/include'
$env:LIBRARY_PATH = Join-Path $appDir 'gcc/lib'
$env:PATH = "$(Join-Path $appDir 'gcc/bin');$appDir;$env:SystemRoot\System32;$env:SystemRoot"
$src = Join-Path $work 'standalone.lua'
$exe = Join-Path $work 'standalone.exe'
Set-Content -LiteralPath $src -Value "print('PORTABLE GCC AOT OK')" -Encoding utf8
$aot = & (Join-Path $appDir 'luac.exe') --cc (Join-Path $appDir 'gcc/bin/gcc.exe') --aot $src -o $exe 2>&1
if ($LASTEXITCODE -ne 0) { throw "Portable GCC AOT failed: $($aot -join ' ')" }
$aotResult = & $exe 2>&1
if ($LASTEXITCODE -ne 0 -or ($aotResult -join ' ') -notmatch 'PORTABLE GCC AOT OK') { throw "Portable GCC executable failed: $($aotResult -join ' ')" }
# Restore the clean PATH before starting the launcher.
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
# Also test the actual double-clickable launcher, independently of system Java.
$marker = Join-Path $work 'launcher-ready.txt'
$stdout = Join-Path $work 'launcher-stdout.txt'
$stderr = Join-Path $work 'launcher-stderr.txt'
$proc = Start-Process -FilePath (Join-Path $appDir 'LuaC-Studio.exe') -ArgumentList @('--launcher-test', "`"$marker`"") -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    $deadline = (Get-Date).AddSeconds(30)
    do { Start-Sleep -Milliseconds 500; $proc.Refresh() } while (!$proc.HasExited -and !(Test-Path $marker) -and (Get-Date) -lt $deadline)
    if (!(Test-Path $marker)) {
        $detail = (Get-Content $stderr -ErrorAction SilentlyContinue) -join ' '
        throw "Packaged launcher failed; exited=$($proc.HasExited), stderr=$detail"
    }
    $resolved = (Get-Content $marker -Raw).Trim()
    if ($resolved -ne $appDir) { throw "Launcher chose wrong application directory: $resolved (expected $appDir)" }
    Write-Output 'Extracted Java launcher window and bundled GCC: OK'
} finally { if (!$proc.HasExited) { $proc.Kill(); $proc.WaitForExit() } }
