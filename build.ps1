$ErrorActionPreference = 'Stop'
py -3 -m PyInstaller --noconfirm --clean --onefile --windowed --name KUFReplayLauncher src/launcher.py
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed' }
$digest = (Get-FileHash -LiteralPath 'dist/KUFReplayLauncher.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $PWD 'dist/KUFReplayLauncher.exe.sha256'), "$digest  KUFReplayLauncher.exe`n")
