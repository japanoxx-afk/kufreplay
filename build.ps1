param([string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python314\python.exe")
$ErrorActionPreference = 'Stop'
$pythonArguments = @()
if (-not (Test-Path -LiteralPath $Python)) {
    $Python = (Get-Command py -ErrorAction Stop).Source
    $pythonArguments = @('-3')
}
$launcherVersion = & $Python @pythonArguments -c "import sys; sys.path.insert(0, 'src'); from updater import VERSION; print(VERSION)"
if ($LASTEXITCODE -ne 0) { throw 'Unable to read launcher version' }
$releaseDirectory = Join-Path $PWD "dist/$launcherVersion"
cmd /c native\build.cmd
if ($LASTEXITCODE -ne 0) { throw 'Native engine build failed' }
& $Python @pythonArguments -m PyInstaller --noconfirm --clean --onefile --windowed --distpath $releaseDirectory --add-binary 'dist/native/KUFReplayEngine.dll;native' --add-binary 'dist/native/KUFReplayInjector.exe;native' --name KUFReplayLauncher src/launcher.py
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed' }
$launcherFile = Join-Path $releaseDirectory 'KUFReplayLauncher.exe'
$digest = (Get-FileHash -LiteralPath $launcherFile -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$launcherFile.sha256", "$digest  KUFReplayLauncher.exe`n")
