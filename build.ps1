$ErrorActionPreference = 'Stop'
$launcherVersion = py -3 -c "import sys; sys.path.insert(0, 'src'); from updater import VERSION; print(VERSION)"
if ($LASTEXITCODE -ne 0) { throw 'Unable to read launcher version' }
$releaseDirectory = Join-Path $PWD "dist/$launcherVersion"
cmd /c native\build.cmd
if ($LASTEXITCODE -ne 0) { throw 'Native engine build failed' }
py -3 -m PyInstaller --noconfirm --clean --onefile --windowed --distpath $releaseDirectory --add-binary 'dist/native/KUFReplayEngine.dll;native' --add-binary 'dist/native/KUFReplayInjector.exe;native' --name KUFReplayLauncher src/launcher.py
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed' }
$launcherFile = Join-Path $releaseDirectory 'KUFReplayLauncher.exe'
$digest = (Get-FileHash -LiteralPath $launcherFile -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$launcherFile.sha256", "$digest  KUFReplayLauncher.exe`n")
